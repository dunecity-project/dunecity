#!/usr/bin/env python3
"""Audit/continue a private power save on the committed cluster source.

Frozen --game-options uses [Research Lab Save] SHA256/GzipBase64. Optional
[Power Config] GzipBase64 contains a QuantBot INI overlay for genome candidates.
The private saves and overlays are input data, never repository fixtures.
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
FIELDS = KEY + ('sha256', 'technology_installments', 'wall_s', 'error')


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def power_summary(measurements, events_path):
    loaded = {row['data']['house']: row['data'] for row in measurements
              if row['marker'] == 'POWER_SAVE_HOUSE' and row['data']['phase'] == 'loaded'}
    finished = {row['data']['house']: row['data'] for row in measurements
                if row['marker'] == 'POWER_SAVE_HOUSE' and row['data']['phase'] == 'finished'}
    result = {}
    for house, initial in loaded.items():
        final = finished.get(house, initial)
        result[house] = {
            'house': house, 'start_credits': initial['credits'], 'end_credits': final['credits'],
            'start_power': initial['produced'], 'start_required': initial['required'],
            'end_power': final['produced'], 'end_required': final['required'],
            'built_wind': final['generators']['Windtrap']['built'] - initial['generators']['Windtrap']['built'],
            'built_nuclear': final['generators']['Nuclear']['built'] - initial['generators']['Nuclear']['built'],
            'generator_spent': final.get('economy', {}).get('generator_spent', 0)
                               - initial.get('economy', {}).get('generator_spent', 0),
            'wind_orders': 0, 'nuclear_orders': 0, 'compact_choices': 0,
            'funded_nuclear_rejections': 0, 'first_nuclear_order_elapsed_cycles': None,
        }
    if events_path.exists():
        with gzip.open(events_path, 'rt') as stream:
            for line in stream:
                event = json.loads(line)
                house, kind, data = event['house'], event['event'], event['data']
                if house not in result:
                    continue
                row = result[house]
                if kind == 'city_generator_choice':
                    row['compact_choices'] += int(bool(data.get('compact_nuclear')))
                    # The two archived saves use no concrete; their reactor's
                    # complete founded quote is exactly its recorded price.
                    row['funded_nuclear_rejections'] += int(
                        not data['nuclear'] and data['nuclear_available'] and data['nuclear_site']
                        and data['cash'] >= data['nuclear_price'])
                elif kind == 'production_order' and data['accepted']:
                    if data['item_name'] == 'Windtrap':
                        row['wind_orders'] += 1
                    elif data['item_name'] == 'Nuclear':
                        row['nuclear_orders'] += 1
                        if row['first_nuclear_order_elapsed_cycles'] is None:
                            row['first_nuclear_order_elapsed_cycles'] = event['cycle'] - loaded[house]['cycle']
    return list(result.values())


def compact_behavior_passes(summary):
    eligible = [row for row in summary if row['compact_choices']]
    return (bool(eligible) and all(not row['funded_nuclear_rejections'] for row in summary)
            and all(row['nuclear_orders'] for row in eligible)
            and any(row['built_nuclear'] and row['generator_spent'] > 0 for row in eligible))


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
    packed = configparser.ConfigParser(interpolation=None)
    if not args.game_options:
        raise ValueError('Playing an exact save requires the frozen --game-options input')
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Research Lab Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Research Lab Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    out = scratch / arm
    out.mkdir(parents=True, exist_ok=True)
    saved = out / 'power-save.dls'
    saved.write_bytes(data)
    source_profile = out / 'fixture-profile'
    restore_fixture_revisions(source_profile, [saved])
    run = out / 'run'
    env = dict(os.environ, BALANCE_POWER_CONTINUATION='1')
    env.pop('BALANCE_TECHNOLOGY_INSTALLMENTS', None)
    env.pop('BALANCE_POWER_CONFIG', None)
    env.pop('BALANCE_POWER_SAVE_AUDIT', None)
    if args.technology_installments != 'saved':
        env['BALANCE_TECHNOLOGY_INSTALLMENTS'] = '1' if args.technology_installments == 'on' else '0'
    if arm.startswith('audit'):
        env['BALANCE_POWER_SAVE_AUDIT'] = '1'
    if packed.has_section('Power Config'):
        overlay = out / 'candidate.ini'
        overlay.write_bytes(gzip.decompress(base64.b64decode(packed['Power Config']['GzipBase64'], validate=True)))
        env['BALANCE_POWER_CONFIG'] = str(overlay)
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
        with log.open('rb') as source, gzip.open(results / (arm + '.log.gz'), 'wb') as dest:
            shutil.copyfileobj(source, dest)
    (results / (arm + '-runner.log')).write_text(process.stdout + process.stderr)
    for events in (run / 'profile').rglob('events.jsonl'):
        with events.open('rb') as source, gzip.open(results / (arm + '-events.jsonl.gz'), 'wb') as dest:
            shutil.copyfileobj(source, dest)
    measurements = []
    for line in lines:
        prefix = 'POWER_SAVE_' if 'POWER_SAVE_' in line else 'RESEARCH_LAB_SAVE_HOUSE:'
        if prefix not in line:
            continue
        marker, body = line[line.index(prefix):].split(':', 1)
        measurements.append({'marker': marker, 'data': json.loads(body) if body.strip().startswith('{') else body.strip()})
    (results / (arm + '-measurements.json')).write_text(json.dumps(measurements, indent=2) + '\n')
    summary = power_summary(measurements, results / (arm + '-events.jsonl.gz'))
    (results / (arm + '-power-summary.json')).write_text(json.dumps(summary, indent=2) + '\n')
    passed = process.returncode == 0 and any('POWER_SAVE_RESULT:' in line for line in lines)
    behavior = not args.verify_compact or arm.startswith('audit') or compact_behavior_passes(summary)
    row = {'arm': arm, 'sha256': digest, 'technology_installments': args.technology_installments,
           'wall_s': round(time.time() - started, 2),
           'error': 'rc=' + str(process.returncode) if not passed else '' if behavior else 'compact_behavior'}
    path = results / 'games.csv'
    exists = path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        writer.writerow(row)
    finish_game(scratch, out, not (passed and behavior))
    return row


def report(args):
    games = read(Path(args.results) / 'games.csv')
    failed = [row for row in games if row['error']]
    behavior_failed = []
    if args.verify_compact:
        wanted = {arm for arm in args.arms if not arm.startswith('audit')}
        seen = {row['arm'] for row in games}
        behavior_failed.extend(sorted(wanted - seen))
        for row in games:
            if row['arm'] not in wanted or row['error']:
                continue
            path = Path(args.results) / (row['arm'] + '-power-summary.json')
            if not path.exists() or not compact_behavior_passes(json.loads(path.read_text())):
                behavior_failed.append(row['arm'])
        print('Compact power behavior: %s%s' % (
            'FAILED' if behavior_failed else 'PASS',
            ' (' + ', '.join(behavior_failed) + ')' if behavior_failed else ''))
    print('Exact-save power continuations: %d passed, %d failed' % (len(games) - len(failed), len(failed)))
    for row in games:
        print('%s: sha256=%s wall=%ss error=%s' % (row['arm'], row['sha256'], row['wall_s'], row['error'] or 'none'))
    return bool(failed or behavior_failed)


def run(args):
    require_free_space()
    with scratch_dir('power-continuation', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        done = {row['arm'] for row in read(Path(args.results) / 'games.csv') if not row['error']}
        for key in schedule(args):
            if key[0] not in done:
                play(args, scratch, binary, key)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results', required=True)
parser.add_argument('--game-options', help='Frozen save/candidate INI; required for play, not shared probe preparation')
parser.add_argument('--arms', nargs='+', choices=('audit', 'baseline', 'audit-off', 'audit-on', 'off', 'on'), default=['audit', 'baseline'])
parser.add_argument('--style-prefix', default='')
parser.add_argument('--technology-installments', choices=('saved', 'off', 'on'), default='saved',
                    help='Explicit post-load payment-rule audit; saved preserves the exact archived rules')
parser.add_argument('--minutes', type=int, default=15)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--capture-mib', type=int, default=512)
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
parser.add_argument('--verify-compact', action='store_true',
                    help='Require funded eligible houses to order reactors, no funded wind rejections, and paid completion')
args = parser.parse_args()
sys.exit(report(args) if args.report else run(args))
