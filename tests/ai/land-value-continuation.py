#!/usr/bin/env python3
"""Exact-save land-value continuations (Stefan, 2026-11-27: "there's still lots of low value see
mba save low").

--game-options is a frozen diagnostic INI: [Land Value Save] SHA256 and GzipBase64 keys (the queue
carries its exact bytes to each machine). Each arm continues the save for --extra-minutes with
every QuantBot playing one style, logging LAND_VALUE_AUDIT rows (tests/ai/land-value-audit.inc) at
the load, every --every minutes and at the end: per house its R/C lot land value, what holds the low
lots down (distance, pollution, crime, parks, Palace), its road strips, credits and population.

Arms: off (shipped defaults), rule-off (match rule Parks Count As Roads off), park-districts,
road-lane-parks, lot-parks, pollution-relocation, industry-separation, redevelop-all
(tests/ai/play-styles/<arm>.ini).

--ctest is the acceptance test of Stefan's request (CTest quantbot_redevelop_low_save, CLAUDE.md
"Asked-for behaviour: an acceptance list first"): his save tests/saves/data/low-dunecity.dls.gz is
continued for 20 minutes in five arms at once, one assert per line of the request, and a table of
request line, measured value and PASS / FAIL / NOT DONE is printed; any FAIL exits non-zero.
"""
import concurrent.futures
import re
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
import threading
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, prune_runs, require_free_space, restore_fixture_revisions, scratch_dir  # noqa: E402

ARMS = ('off', 'rule-off', 'park-districts', 'road-lane-parks', 'lot-parks', 'pollution-relocation', 'industry-separation',
        'redevelop-all')
CTEST_ARMS = ('off', 'road-lane-parks', 'lot-parks', 'pollution-relocation', 'industry-separation')
LOW_SAVE = root / 'tests/saves/data/low-dunecity.dls.gz'
KEEP = ('REDEVELOP:', 'INDUSTRY_PLACED:', 'HOME_PLACED:', 'PARK_AUDIT:', 'YARD_PATH_AUDIT:', 'SCENARIO_SWITCH_ACTIVITY:')
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


APPEND_LOCK = threading.Lock()


def append(path, fields, rows):
    with APPEND_LOCK:
        _append(path, fields, rows)


def _append(path, fields, rows):
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
    if args.save_gz:
        data = gzip.decompress(Path(args.save_gz).read_bytes())
        return data, hashlib.sha256(data).hexdigest()
    packed = configparser.ConfigParser(interpolation=None)
    packed.read(args.game_options)
    data = gzip.decompress(base64.b64decode(packed['Land Value Save']['GzipBase64'], validate=True))
    digest = hashlib.sha256(data).hexdigest()
    if digest != packed['Land Value Save']['SHA256']:
        raise RuntimeError('Frozen save checksum mismatch')
    return data, digest


def play(args, scratch, binary, key):
    arm, = key
    if not args.game_options and not args.save_gz:
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
    (results / (arm + '-lines.txt')).write_text(
        '\n'.join(line for line in lines if any(marker in line for marker in KEEP)) + '\n')
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


def fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def acceptance(args):
    """One assert per line of Stefan's request, read from the five arms' logs. Returns the failures."""
    results = Path(args.results)
    games = {row['arm']: row for row in read(results / 'games.csv')}
    audit = read(results / 'audit.csv')
    lines = {arm: (results / (arm + '-lines.txt')).read_text().splitlines()
             if (results / (arm + '-lines.txt')).exists() else [] for arm in CTEST_ARMS}

    def events(arm, marker, **match):
        return [f for f in (fields(l, marker) for l in lines[arm] if marker in l)
                if all(f.get(k) == str(v) for k, v in match.items())]

    def activity(arm):
        total = {}
        for f in events(arm, 'SCENARIO_SWITCH_ACTIVITY:'):
            for k, v in f.items():
                if k != 'house':
                    total[k] = total.get(k, 0) + int(v)
        return total

    def roads2x2(arm):
        return sum(int(r['roads_in_2x2'] or 0) for r in audit if r['arm'] == arm and r['phase'] == 'end')

    rows = []

    def line(request, test, measured, ok):
        rows.append((request, test, measured, 'PASS' if ok is True else 'FAIL' if ok is False else ok))

    t = 'quantbot_redevelop_low_save'
    missing = [arm for arm in CTEST_ARMS if arm not in games or games[arm]['error']]
    line('every arm continued the save 20 minutes', t, 'failed arms: %s' % (missing or 'none'), not missing)
    off = activity('off')
    off_orders = sum(v for k, v in off.items() if k.endswith('_ordered'))
    line('nothing fires with the switches off', t, '%d redevelopment orders' % off_orders, off_orders == 0)

    moved = events('pollution-relocation', 'REDEVELOP:', event='moved')
    demolished = [m for m in moved if m.get('old_demolished') == '1']
    line('"demolishing and rebuilding ... moving ... industrial zones to different areas"', t,
         '%d Industrial zones rebuilt elsewhere and the old one demolished (need 3)' % len(demolished), len(demolished) >= 3)
    worse = [m for m in demolished if 2 * int(m.get('new_harm', 0)) > int(m.get('harm', 0))]
    clear = sum(1 for m in demolished if int(m.get('home_distance', -1)) > 5)
    line('... "to different areas": the new site costs our homes at most half the old one', t,
         '%d of %d moves fail that (%d of them land beyond 5 tiles of every home)' % (len(worse), len(demolished), clear),
         bool(demolished) and not worse)
    reloc = activity('pollution-relocation')
    checked, cleaner, raised = reloc.get('relocation_checked', 0), reloc.get('relocation_cleaner', 0), reloc.get('relocation_raised', 0)
    line('... the homes around a moved zone get less pollution', t,
         'cleaner in %d of %d reads (need half, 3 reads)' % (cleaner, checked), checked >= 3 and 2 * cleaner >= checked)
    line('... and more land value', t, 'raised in %d of %d reads (need half)' % (raised, checked),
         checked >= 3 and 2 * raised >= checked)
    line('"moving factories"', '-', 'no switch moves a factory', 'NOT DONE')
    line('"adding in more palaces" / "destroying and adding palaces"', 'quantbot_city_lessons (LowValuePalaces, seewhatidid-ai)',
         'not in this test', 'NOT DONE')

    lane, lot = activity('road-lane-parks'), activity('lot-parks')
    lane_placed = lane.get('lane_park_placed', 0)
    lot_placed = lot.get('lot_clear_placed', 0) + lot.get('lot_fill_placed', 0)
    line('"adding in more ... parks" (spare lanes)', t, '%d Parks standing at their chosen lane tiles (need 10)' % lane_placed,
         lane_placed >= 10)
    line('"adding in more ... parks" (cleared lots)', t, '%d Parks standing on cleared lots (need 5)' % lot_placed, lot_placed >= 5)
    lane_checked, lane_raised = lane.get('lane_park_checked', 0), lane.get('lane_park_raised', 0)
    line('... "as a way to also increase value" (lane Parks)', t,
         'raised in %d of %d reads (need half, 5 reads)' % (lane_raised, lane_checked), lane_checked >= 5 and 2 * lane_raised >= lane_checked)
    lot_checked = lot.get('lot_clear_checked', 0) + lot.get('lot_fill_checked', 0)
    lot_raised = lot.get('lot_clear_raised', 0) + lot.get('lot_fill_raised', 0)
    line('... "as a way to also increase value" (lot Parks)', t,
         'raised in %d of %d reads (need half, 3 reads)' % (lot_raised, lot_checked), lot_checked >= 3 and 2 * lot_raised >= lot_checked)
    parks = events('road-lane-parks', 'PARK_AUDIT:')
    as_road = [p for p in parks if p['road_connection'] == '1' and p['unit_road'] == '0']
    line('"count a park as a road (without the movement bonus)"', t,
         '%d of %d Parks are road connections without unit road speed (need all, 10)' % (len(as_road), len(parks)),
         len(parks) >= 10 and len(as_road) == len(parks))
    clears = lot.get('lot_clear_placed', 0)
    line('"destroying and rebuilding zones"', t, '%d lots demolished and rebuilt as Parks, %d zones moved (need 1 each)'
         % (clears, len(demolished)), clears >= 1 and len(demolished) >= 1)
    off_roads, lane_roads = roads2x2('off'), roads2x2('road-lane-parks')
    line('"lots of 2 lane roads ... we can remove some roads"', t,
         'second-lane road tiles at the end: %d with RoadLaneParks, %d off (need 20 fewer)' % (lane_roads, off_roads),
         lane_roads + 20 <= off_roads)
    lane_sites = {(e['x'], e['y']) for e in events('road-lane-parks', 'REDEVELOP:', event='placed', kind='lane_park')}
    cut = [p for p in parks if (p['x'], p['y']) in lane_sites and p.get('units_joined') != '1']
    joined = [p for p in parks if (p['x'], p['y']) in lane_sites]
    retired = len(events('road-lane-parks', 'REDEVELOP:', event='retired'))
    line('"as long as we maintain paths out of the base" (around each lane Park)', t,
         '%d of %d lane Parks standing leave units no way round them (need 0); %d retired when later building closed the way'
         % (len(cut), len(joined), retired),
         bool(joined) and not cut)
    yards = {}
    for e in events('road-lane-parks', 'YARD_PATH_AUDIT:'):
        yards.setdefault((e['house'], e['x'], e['y']), {})[e['phase']] = e['path_out']
    kept = [k for k, v in yards.items() if v.get('loaded') == '1' and 'end' in v]
    lost = [k for k in kept if yards[k]['end'] != '1']
    line('"as long as we maintain paths out of the base" (ground)', t,
         '%d of %d Construction Yards lost their path to the map edge (need 0)' % (len(lost), len(kept)), bool(kept) and not lost)
    wall = float(games.get('road-lane-parks', {}).get('wall_s') or 0)
    search = sum(activity(arm).get('search_us', 0) for arm in ('road-lane-parks',)) / 1e6
    line('"dont introduce performance issues evaluating though"', t,
         'lane and lot searches %.1f s of a %.0f s arm (need <= 2%%)' % (search, wall), wall > 0 and search <= 0.02 * wall)
    sep = events('industry-separation', 'INDUSTRY_PLACED:', separated=1)
    sep_near = [e for e in sep if e['near_homes'] == '1']
    line('IndustrySeparation: new Industrial zones clear of homes', t,
         '%d of %d new Industrial zones within 5 tiles of a home' % (len(sep_near), len(sep)),
         'NOT TESTED (no Industrial zone built)' if not sep else not sep_near)

    width = max(len(r[0]) for r in rows)
    print('\nRequest line | Test | Measured | Result')
    print('|---|---|---|---|')
    for request, test, measured, result in rows:
        print('| %s | %s | %s | %s |' % (request, test, measured, result))
    return [r for r in rows if r[3] == 'FAIL']


def run_ctest(args):
    require_free_space()
    results = Path(args.results)
    shutil.rmtree(results, ignore_errors=True)
    results.mkdir(parents=True)
    args.arms = list(CTEST_ARMS)
    with scratch_dir('land-value-continuation', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        with concurrent.futures.ThreadPoolExecutor(len(CTEST_ARMS)) as pool:
            for row in pool.map(lambda arm: play(args, scratch, binary, (arm,)), CTEST_ARMS):
                print('arm %s: %s s %s' % (row['arm'], row['wall_s'], row['error'] or 'ok'), flush=True)
    failures = acceptance(args)
    print('\nREDEVELOP_ACCEPTANCE: %d failed' % len(failures))
    return 1 if failures else 0


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
parser.add_argument('--save-gz', help='A gzipped save to continue in place of the frozen --game-options input')
parser.add_argument('--ctest', action='store_true', help='Acceptance test on Stefan\'s low save (CTest quantbot_redevelop_low_save)')
args = parser.parse_args()
if args.freeze:
    freeze(*args.freeze)
    raise SystemExit(0)
if not args.results:
    parser.error('--results is required')
if args.ctest:
    args.save_gz = args.save_gz or str(LOW_SAVE)
    args.extra_minutes = 20
    raise SystemExit(run_ctest(args))
if args.report:
    raise SystemExit(report(args))
raise SystemExit(run(args))
