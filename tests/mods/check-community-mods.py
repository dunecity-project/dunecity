#!/usr/bin/env python3
"""Play every bundled community revision twice; require equal digests and real production."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMBAT_UNITS = {28, 29, 33, 35, 36, 39, 40, 41, 42, 43}


def production_map(path):
    # Only two yards exist initially. A factory, power plant or combat vehicle
    # therefore proves production (infantry from destroyed structures do not).
    # Separate rock/spice islands keep this a five-minute production smoke rather
    # than an early-conquest race. This is not a balance/combat benchmark.
    size = 256
    rows = []
    for y in range(size):
        row = '%' * 64 + '~' * 16 + '@' * 96 + '~' * 16 + '%' * 64
        rows.append(f'{y:03}={row}')
    path.write_text('[BASIC]\nVersion=2\nTechLevel=9\nWinFlags=3\nLoseFlags=1\n'
                    'TimeOut=0\n[MAP]\nSizeX=256\nSizeY=256\n' + '\n'.join(rows)
                    + '\n[Player1]\nCredits=50000\nBrain=Team1\n'
                    '[Player2]\nCredits=50000\nBrain=Team2\n'
                    '[STRUCTURES]\nID000=Player1,Const Yard,256,5140\n'
                    'ID001=Player2,Const Yard,256,60396\n', encoding='ascii')


def check_payload(entry):
    directory = ROOT / 'mods' / entry['folder']
    expected = entry['files']
    for name, record in expected.items():
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise RuntimeError(f'{entry["name"]}: missing/non-regular file {name}')
        data = path.read_bytes()
        if len(data) != record['size'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise RuntimeError(f'{entry["name"]}: catalogue file changed: {name}')
    extras = {p.relative_to(directory).as_posix() for p in directory.rglob('*') if p.is_file()} - set(expected)
    if extras != {'workshop-revision.ini'}:
        raise RuntimeError(f'{entry["name"]}: unexpected payload files: {sorted(extras)}')


def play(entry, attempt, args, map_path):
    out = args.output_dir / f'{entry["folder"]}-{attempt}'
    command = [sys.executable, str(ROOT / 'tests/ai/run-campaign-balance.py'),
               '--build-dir', str(args.build_dir), '--output-dir', str(out), '--repeatable',
               '--custom-map', str(map_path), '--mod', entry['folder'], '--local-spectator',
               '--game-options', str(ROOT / 'mods' / entry['folder'] / 'GameOptions.ini'),
               '--scenario', '--scenario-sides', 'harkonnen:1:qBotHard,atreides:2:qBotHard',
               '--minutes', '5', '--seed', '4242', '--no-ai-telemetry',
               '--enemy-difficulty', 'hard', '--partner-difficulty', 'hard']
    result = subprocess.run(command, capture_output=True, text=True)
    logs = sorted(out.rglob('run.log'), key=lambda p: p.stat().st_mtime)
    log = logs[-1].read_text(errors='replace') if logs else ''
    digests = re.findall(r'PROBE_DIGEST: cycle=(\d+) digest=([0-9a-f]+) final=1', log)
    if result.returncode or len(digests) != 1 or int(digests[0][0]) < 18750:
        raise RuntimeError(f'{entry["name"]}: five-minute game failed (rc={result.returncode}); '
                           + result.stderr[-1000:])
    buildings = {int(item) for item in re.findall(r'CAMPAIGN_BUILDING_AUDIT: house=\d+ item=(\d+)', log)}
    units = {int(item) for item in re.findall(r'CAMPAIGN_UNIT_AUDIT: house=\d+ id=\d+ item=(\d+)', log)}
    if not ({4, 7} & buildings) or 18 not in buildings or not (units & COMBAT_UNITS):
        raise RuntimeError(f'{entry["name"]}: no factory/power/combat-unit production: '
                           f'buildings={sorted(buildings)} units={sorted(units)}')
    return {'digest': digests[0][1], 'cycle': int(digests[0][0]),
            'buildings': sorted(buildings), 'units': sorted(units)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    map_path = args.output_dir / 'production.ini'
    production_map(map_path)
    entries = json.loads((ROOT / 'mods/community-mods.json').read_text())['mods']
    results = {}
    for entry in entries:
        check_payload(entry)
        pair = [play(entry, attempt, args, map_path) for attempt in (1, 2)]
        if pair[0]['digest'] != pair[1]['digest']:
            raise RuntimeError(f'{entry["name"]}: repeated game digests differ: {pair}')
        results[entry['name']] = pair
        print(entry['name'], json.dumps(pair), flush=True)
        (args.output_dir / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'{len(entries)} community mods: five minutes twice, equal digests, production confirmed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
