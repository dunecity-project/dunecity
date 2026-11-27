#!/usr/bin/env python3
"""Per-house audit of an exact save, read by the game itself (Stefan, 2026-11-27: "i saved a game
seewhatidid on the mba for metropolis where i added more palaces to improve land, added rocket turrets
as a maginot and built a lot of launchers ... analyse the save and the logs").

--game-options is a frozen save INI written by tests/ai/land-value-continuation.py --freeze ([Land Value
Save] SHA256 and GzipBase64; the queue carries its bytes). Each arm loads the save headless with the
dummy driver and logs SAVE_HOUSE_AUDIT rows (tests/ai/save-house-audit.inc) per house - players, credits,
city, units and structures by type, Palaces with the land value of their lots, Rocket Turrets against the
enemy - with each house's UNIT_ECONOMY combat record (houses.csv with each row's JSON, units.csv; every
arm's rows, also when the queue collects them; houses.jsonl only for a local run). With --extra-minutes the
AI plays on and is audited every 10 minutes, at each --audit-at minute and at the end. Arm 'audit' plays
the shipped defaults; any other --arms name is a QuantBot play style (tests/ai/play-styles/<arm>.ini) for
every house. --arm NAME:HOUSE=STYLE[,HOUSE=STYLE][;buildings=0|1] is a named arm in which only those
houses play those styles (the others keep the shipped defaults), optionally with match rule Turrets Can
Fire On Buildings set (a 1.0.880 save let turrets hit buildings), e.g.
    --arm drill-bld:0=turret-drill;buildings=1 --arm control-bld:;buildings=1 --extra-minutes 48 --audit-at 23 48
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
FIELDS = KEY + ('sha256', 'extra_minutes', 'wall_s', 'error')
# houses.csv: one row per house and audit, the whole SAVE_HOUSE_AUDIT object in 'json'. The cluster appends
# every game's CSV tables into the job's results; a .jsonl file is kept from one game only.
HOUSE_FIELDS = ('arm', 'phase', 'minute', 'house', 'json')
ECONOMY = ('arm', 'phase', 'house', 'item', 'name', 'kind', 'price', 'built', 'lost', 'damage_value', 'kill_bonus',
           'kills', 'hits', 'cycle')


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


def arm_specs(args):
    """{name: (house styles 'h=style,h=style', buildings rule '' / '0' / '1')} of the --arm options."""
    specs = {}
    for spec in args.arm:
        name, _, rest = spec.partition(':')
        styles, _, rule = rest.partition(';')
        buildings = rule.split('=', 1)[1] if rule.startswith('buildings=') else ''
        if not name or (rule and not rule.startswith('buildings=')) or buildings not in ('', '0', '1'):
            raise ValueError('--arm NAME:HOUSE=STYLE[,HOUSE=STYLE][;buildings=0|1], got ' + spec)
        specs[name] = (styles, buildings)
    return specs


def schedule(args):
    return [(arm,) for arm in list(args.arms) + list(arm_specs(args))]


def frozen_save(args):
    packed = configparser.ConfigParser(interpolation=None)
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Land Value Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Land Value Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    return data, digest


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
        raise ValueError('Auditing an exact save requires the frozen --game-options input')
    out = scratch / arm
    out.mkdir(parents=True, exist_ok=True)
    data, digest = frozen_save(args)
    saved = out / 'audit.dls'
    saved.write_bytes(data)
    source_profile = out / 'fixture-profile'
    restore_fixture_revisions(source_profile, [saved])
    run = out / 'run'
    env = dict(os.environ, BALANCE_SAVE_HOUSE_AUDIT='1')
    if args.extra_minutes:
        env['BALANCE_SAVE_HOUSE_AUDIT_EXTRA'] = str(args.extra_minutes)
    if args.audit_at:
        env['BALANCE_SAVE_HOUSE_AUDIT_AT'] = ','.join(str(m) for m in args.audit_at)
    specs = arm_specs(args)
    if arm in specs:
        styles, buildings = specs[arm]
        if styles:
            env['BALANCE_HOUSE_STYLES'] = styles
        if buildings:
            env['BALANCE_TURRETS_FIRE_ON_BUILDINGS'] = buildings
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(run), '--load-save', str(saved),
               '--save-profile', str(source_profile), '--mod', 'dunecity', '--minutes', '1440',
               '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--capture-mib', str(args.capture_mib)]
    if arm != 'audit' and arm not in specs:
        command += ['--scenario-style', arm]
    started = time.time()
    process = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    log = run / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    (results / (arm + '-runner.log')).write_text(process.stdout[-20000:] + process.stderr[-20000:])
    houses, economy = [], []
    for line in lines:
        if 'SAVE_HOUSE_AUDIT:' in line:
            houses.append(dict(json.loads(line.split('SAVE_HOUSE_AUDIT:', 1)[1]), arm=arm))
        elif 'UNIT_ECONOMY:' in line:
            row = dict(p.split('=', 1) for p in line.split('UNIT_ECONOMY:', 1)[1].split() if '=' in p)
            economy.append(dict({f: row.get(f, '') for f in ECONOMY}, arm=arm))
    with (results / 'houses.jsonl').open('a') as handle:
        for row in houses:
            handle.write(json.dumps(row, sort_keys=True) + '\n')
    if houses:
        append(results / 'houses.csv', HOUSE_FIELDS, [{'arm': arm, 'phase': row['phase'], 'minute': row['minute'],
                                                      'house': row['house'], 'json': json.dumps(row, sort_keys=True)}
                                                     for row in houses])
    if economy:
        append(results / 'units.csv', ECONOMY, economy)
    passed = process.returncode == 0 and any(r['phase'] == 'loaded' for r in houses)
    if log.exists():
        with log.open('rb') as source, gzip.open(results / (arm + '-run.log.gz'), 'wb') as dest:
            shutil.copyfileobj(source, dest)
    row = {'arm': arm, 'sha256': digest, 'extra_minutes': args.extra_minutes,
           'wall_s': round(time.time() - started, 2), 'error': '' if passed else 'rc=' + str(process.returncode)}
    append(results / 'games.csv', FIELDS, [row])
    finish_game(scratch, out, not passed)
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    failed = [row for row in rows if row['error']]
    print('Save house audit: %d passed, %d failed' % (len(rows) - len(failed), len(failed)))
    for line in read(Path(args.results) / 'houses.csv'):
        row = json.loads(line['json'])
        print('%-8s %-8s m%-6s h%s players=%s credits=%s lv=%s low=%s/%s turrets=%s bands=%s units=%s' % (
            row['arm'], row['phase'], row['minute'], row['house'], [p['class'] for p in row['players']], row['credits'],
            row['avg_land_value'], row['low_lots'], row['lots'], row['rocket_turrets'], row['turret_enemy_bands'],
            row['combat_units']))
    return bool(failed)


def run(args):
    require_free_space()
    with scratch_dir('save-house-audit', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        done = {row['arm'] for row in read(Path(args.results) / 'games.csv') if not row['error']}
        for key in schedule(args):
            if key[0] not in done:
                play(args, scratch, binary, key)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results')
parser.add_argument('--game-options', help='Frozen save input; required for play, not shared probe compilation')
parser.add_argument('--arms', nargs='*', default=['audit'])
parser.add_argument('--arm', action='append', default=[], help='NAME:HOUSE=STYLE[,HOUSE=STYLE][;buildings=0|1]')
parser.add_argument('--audit-at', nargs='+', type=int, default=[], help='Minutes after the load to audit at')
parser.add_argument('--extra-minutes', type=int, default=0)
parser.add_argument('--wall-timeout', type=int, default=5400)
parser.add_argument('--capture-mib', type=int, default=256)
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
if not args.results:
    parser.error('--results is required')
if args.report:
    raise SystemExit(report(args))
raise SystemExit(run(args))
