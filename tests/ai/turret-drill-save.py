#!/usr/bin/env python3
"""The turret drill against Stefan's own demonstration (CTest turret_drill_save).

Stefan, 2026-11-27: "i just build turrets into their base as a mass and they carved a hole in the ebemy
base. you have definitely not been trialling this properly". CLAUDE.md, "Asked-for behaviour", rules 2 and 3:
the AI with the switch on is started from his earlier save and must reach the measured footprint of what he
did, within a stated tolerance, with one assert per acceptance line.

His save tests/saves/data/seewhatidid-dunecity.dls.gz (Metropolis, game minute 72, house 0 Harkonnen) is
played on for 23 game minutes (to minute 95, his moremaginot save) twice, headless:
  drill    house 0 plays the turret-drill style (TurretDrill on), the other houses the shipped defaults;
  control  every house plays the shipped defaults.
Turrets fire on units only in both (the default rule). What he did by minute 95, measured from his saves
(outputs/seewhatidid-20261127/analysis/acceptance-list.md): 76 more Rocket Turrets standing, 96% touching
another, median 10 tiles to the nearest enemy building, 18 within 8 tiles, +308k R-Turret damage value,
Ordos (engine house 2) lost 276 structures. The tolerance is half of each count and the measured band for the
distances; the effect lines must also beat the control. Prints the acceptance table; exits 1 on any FAIL.
"""
import argparse
import base64
import configparser
import concurrent.futures
import csv
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
SAVE = root / 'tests/saves/data/seewhatidid-dunecity.dls.gz'
ARMS = {'drill': 'drill:0=turret-drill;buildings=0', 'control': 'control:;buildings=0'}
MINUTES = 23
OWN, ORDOS, ENEMIES = 0, 2, (2, 5, 7)

# Stefan's footprint by minute 95 and the tolerance (half his counts; his measured distance band).
HIS = {'new': 76, 'touching': 96, 'standoff': 10, 'within8': 18, 'damage': 308000, 'ordos_lost': 276}
MIN_NEW, MIN_TOUCHING, STANDOFF_BAND, MIN_WITHIN8 = HIS['new'] // 2, 80, (8, 14), HIS['within8'] // 2
MIN_DAMAGE, MIN_ORDOS_LOST = HIS['damage'] // 2, HIS['ordos_lost'] // 2


def freeze(results):
    data = gzip.decompress(SAVE.read_bytes())
    packed = configparser.ConfigParser(interpolation=None)
    packed['Land Value Save'] = {'SHA256': hashlib.sha256(data).hexdigest(),
                                 'GzipBase64': base64.b64encode(gzip.compress(data, mtime=0)).decode()}
    path = results / 'seewhatidid.ini'
    with path.open('w') as handle:
        packed.write(handle)
    return path


def play(args, options, arm):
    out = Path(args.results) / arm
    command = [sys.executable, str(root / 'tests/ai/save-house-audit.py'), '--build-dir', args.build_dir,
               '--results', str(out), '--game-options', str(options), '--arms', '--arm', ARMS[arm],
               '--extra-minutes', str(MINUTES), '--audit-at', str(MINUTES)]
    process = subprocess.run(command, cwd=root, capture_output=True, text=True)
    (Path(args.results) / (arm + '-audit.log')).write_text(process.stdout[-20000:] + process.stderr[-20000:])
    return arm, process.returncode


def measure(results, arm):
    """House 0's turrets and the R-Turret / enemy structure record, load (minute 72) to the audit (95)."""
    out = results / arm
    rows = [json.loads(line) for line in (out / 'houses.jsonl').read_text().splitlines() if line.strip()]
    own = lambda phase: [r for r in rows if r['phase'] == phase and r['house'] == OWN]  # noqa: E731
    loaded, audited = own('loaded'), own('at')
    if not loaded or not audited:
        raise RuntimeError(f'{arm}: no loaded/audit rows in {out}/houses.jsonl')
    start = {(x, y) for x, y, *_ in loaded[0]['turrets']}
    turrets = {(x, y): enemy_building for x, y, enemy_building, _ in audited[0]['turrets']}
    new = [p for p in turrets if p not in start]
    touching = sum(1 for p in new if any(q != p and max(abs(p[0] - q[0]), abs(p[1] - q[1])) <= 1 for q in turrets))
    standoff = sorted(turrets[p] for p in new)
    with (out / 'units.csv').open(newline='') as handle:
        units = list(csv.DictReader(handle))

    def state(phase):
        cycles = sorted({int(r['cycle']) for r in units if r['phase'] == phase})
        return {(int(r['house']), r['name']): r for r in units if r['phase'] == phase and int(r['cycle']) == cycles[0]}

    before, after = state('loaded'), state('at')
    delta = lambda key, field: int(float(after.get(key, {}).get(field, 0) or 0)) - int(  # noqa: E731
        float(before.get(key, {}).get(field, 0) or 0))
    lost = {h: sum(delta((h, n), 'lost') for (hh, n), r in after.items() if hh == h and r['kind'] == 'structure')
            for h in ENEMIES}
    log = gzip.decompress((out / f'{arm}-run.log.gz').read_bytes()).decode(errors='replace')
    count = lambda marker: len(re.findall(rf'{marker}: house={OWN} rule=turret_drill\b', log))  # noqa: E731
    return {'new': len(new), 'touching': 100 * touching // max(1, len(new)),
            'standoff': standoff[len(standoff) // 2] if standoff else None, 'within8': sum(d <= 8 for d in standoff),
            'damage': delta((OWN, 'R-Turret'), 'damage_value'), 'kills': delta((OWN, 'R-Turret'), 'kills'),
            'ordos_lost': lost[ORDOS], 'enemy_lost': sum(lost.values()),
            'ordered': count('TURRET_FRONT'), 'placed': count('TURRET_FRONT_PLACED'),
            'engaged': count('TURRET_FRONT_ENGAGED')}


def acceptance(results):
    drill, control = measure(results, 'drill'), measure(results, 'control')
    rows = []

    def line(request, measured, ok):
        rows.append((request, measured, 'PASS' if ok else 'FAIL'))

    line('the switch fires: drill orders with TurretDrill on, none off',
         f"drill {drill['ordered']} ordered; control {control['ordered']}", drill['ordered'] > 0 and control['ordered'] == 0)
    line('the orders become turrets that fight (placed >= 50% of ordered, engaged >= 80% of placed)',
         f"{drill['placed']} placed, {drill['engaged']} engaged of {drill['ordered']}",
         drill['placed'] * 2 >= drill['ordered'] and drill['engaged'] * 5 >= drill['placed'] * 4)
    line(f"\"build turrets ... as a mass\": >= {MIN_NEW} new turrets standing by minute 95 (his {HIS['new']})",
         f"{drill['new']} (control {control['new']})", drill['new'] >= MIN_NEW)
    line(f"a mass, not a scatter: >= {MIN_TOUCHING}% of them touching another turret (his {HIS['touching']}%)",
         f"{drill['touching']}% (control {control['touching']}%)", drill['touching'] >= MIN_TOUCHING)
    line(f"\"into their base\": median distance to the nearest enemy building {STANDOFF_BAND[0]}-{STANDOFF_BAND[1]} "
         f"tiles (his {HIS['standoff']})", f"{drill['standoff']} (control {control['standoff']})",
         drill['standoff'] is not None and STANDOFF_BAND[0] <= drill['standoff'] <= STANDOFF_BAND[1])
    line(f"... and >= {MIN_WITHIN8} of them within 8 tiles of an enemy building (his {HIS['within8']})",
         f"{drill['within8']} (control {control['within8']})", drill['within8'] >= MIN_WITHIN8)
    line(f"\"carved a hole\": R-Turret damage value minute 72-95 >= {MIN_DAMAGE // 1000}k (his "
         f"{HIS['damage'] // 1000}k) and above the control",
         f"{drill['damage'] // 1000}k, {drill['kills']} kills (control {control['damage'] // 1000}k)",
         drill['damage'] >= MIN_DAMAGE and drill['damage'] > control['damage'])
    line(f"... in the enemy base: Ordos structures lost minute 72-95 >= {MIN_ORDOS_LOST} (his {HIS['ordos_lost']}) "
         f"and above the control", f"{drill['ordos_lost']} (control {control['ordos_lost']}); all enemies "
         f"{drill['enemy_lost']} (control {control['enemy_lost']})",
         drill['ordos_lost'] >= MIN_ORDOS_LOST and drill['ordos_lost'] > control['ordos_lost'])
    print('| request line | measured (drill arm, minute 72 -> 95) | result |')
    print('|---|---|---|')
    for request, measured, result in rows:
        print(f'| {request} | {measured} | {result} |')
    return [r for r in rows if r[2] == 'FAIL']


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', default=str(root / 'build-int'))
    parser.add_argument('--results', required=True)
    parser.add_argument('--report', action='store_true', help='Assert on existing --results without playing')
    args = parser.parse_args()
    results = Path(args.results)
    if not args.report:
        shutil.rmtree(results, ignore_errors=True)
        results.mkdir(parents=True)
        options = freeze(results)
        with concurrent.futures.ThreadPoolExecutor(len(ARMS)) as pool:
            for arm, code in pool.map(lambda arm: play(args, options, arm), ARMS):
                print(f'arm {arm}: save-house-audit rc={code}', flush=True)
                if code:
                    print((results / (arm + '-audit.log')).read_text()[-4000:])
                    print('TURRET_DRILL_ACCEPTANCE: the game did not finish')
                    return 1
    failures = acceptance(results)
    print(f'\nTURRET_DRILL_ACCEPTANCE: {len(failures)} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
