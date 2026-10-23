#!/usr/bin/env python3
"""Every bundled community map (data/maps/community-maps.json) loads and has its advertised starts.

Static checks, for each manifest entry: the file is the recorded catalogue revision's map.ini (SHA-256),
its catalogue sidecar names the item, mod and version, the "<N>P - <W>x<H>" display name matches the
[MAP] size and the lobby slot count (the house sections MapMetadata::countPlayers counts), and every
slot starts with a Construction Yard or an MCV. With --build-dir each map is then played for one game
minute in its mod through tests/ai/run-campaign-balance.py (all slots AI, real game load); every lobby
house must still own a Construction Yard or MCV at the end and the probe must report a final digest.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
SLOT_HOUSES = ('Harkonnen', 'Atreides', 'Ordos', 'Fremen', 'Sardaukar', 'Mercenary', 'Neutral', 'Rebels', 'Custom',
               'Wildspade', 'Kleshmersh', 'Tharpique')
CONSTRUCTION_YARD, MCV = '2', '34'  # include/data.h ItemID_enum


def sections(text):
    result, current = {}, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line[0] in ';#':
            continue
        if line.startswith('['):
            current = line[1:line.find(']')].lower() if ']' in line else line[1:].lower()
            result.setdefault(current, [])
        elif current is not None:
            result[current].append(line)
    return result


def values(lines):
    return {k.strip().lower(): v.strip() for k, _, v in (line.partition('=') for line in lines)}


def check_static(entry, problems):
    path = root / 'data/maps' / entry['directory'] / entry['file']
    if not path.is_file():
        problems.append(f'{entry["file"]}: missing'); return None
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        problems.append(f'{entry["file"]}: bytes differ from catalogue revision {entry["revision"]}')
    # CustomGameMenu skips files over 1 MiB or containing NUL bytes; such a map would never be listed.
    if len(data) > 1024 * 1024 or b'\0' in data:
        problems.append(f'{entry["file"]}: the map chooser skips it (over 1 MiB or NUL bytes)')
    if entry['directory'] not in ('singleplayer', 'multiplayer') or (entry['mod'] == 'dunecity') != (entry['directory'] == 'singleplayer'):
        problems.append(f'{entry["file"]}: Dune City maps belong in singleplayer/, other mods in multiplayer/')
    sidecar = path.with_name(path.name + '.workshop.ini')
    meta = values(sections(sidecar.read_text(encoding='utf-8')).get('workshop', [])) if sidecar.is_file() else {}
    expected = {'name': f'"{entry["name"]}"', 'id': entry['item_id'], 'mod': entry['mod'],
                'version': str(entry['version']), 'immutable': 'true'}
    for key, value in expected.items():
        if meta.get(key) != value:
            problems.append(f'{entry["file"]}: sidecar {key}={meta.get(key)!r}, expected {value!r}')
    match = re.fullmatch(r'(\d+)P - (\d+)x(\d+) - .+', entry['name'])
    if not match or entry['file'] != entry['name'] + '.ini':
        problems.append(f'{entry["file"]}: name does not follow "<N>P - <W>x<H> - <Name>"'); return None
    players, width, height = map(int, match.groups())
    text = data.decode('latin-1')
    secs = sections(text)
    size = values(secs.get('map', []))
    if (int(size.get('sizex', 0)), int(size.get('sizey', 0))) != (width, height):
        problems.append(f'{entry["file"]}: [MAP] is {size.get("sizex")}x{size.get("sizey")}, name says {width}x{height}')
    slots = [h for h in SLOT_HOUSES if h.lower() in secs] + [f'Player{i}' for i in range(1, 13) if f'player{i}' in secs]
    if len(slots) != players or entry['players'] != players:
        problems.append(f'{entry["file"]}: {len(slots)} lobby slots {slots}, name says {players}')
    started = set()
    for line in secs.get('structures', []) + secs.get('units', []):
        fields = [f.strip() for f in line.partition('=')[2].split(',')]
        if len(fields) >= 2 and fields[1].lower() in ('const yard', 'mcv'):
            started.add(fields[0].lower())
    missing = [s for s in slots if s.lower() not in started]
    if missing:
        problems.append(f'{entry["file"]}: slots without a Construction Yard or MCV: {missing}')
    return path, players


def check_game(entry, path, players, build_dir, output, problems):
    out = output / re.sub(r'[^A-Za-z0-9]+', '-', entry['name']).strip('-')
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(build_dir),
               '--output-dir', str(out), '--repeatable', '--custom-map', str(path), '--mod', entry['mod'],
               '--local-spectator', '--free-for-all', '--minutes', '1', '--seed', '4242', '--no-ai-telemetry',
               '--enemy-difficulty', 'hard', '--partner-difficulty', 'hard']
    result = subprocess.run(command, capture_output=True, text=True)
    logs = sorted(out.rglob('run.log'), key=lambda p: p.stat().st_mtime)
    log = logs[-1].read_text(errors='replace') if logs else ''
    if result.returncode != 0 or 'final=1' not in log or 'outcome=' not in log:
        problems.append(f'{entry["file"]}: game did not finish (rc={result.returncode}) {result.stderr[-400:]}')
        return
    # The lobby houses the probe seated (its resolved roster). A map may also place buildings
    # for houses without a lobby slot (4P Big Border's Fremen and Neutral fortresses).
    summaries = sorted(out.rglob('summary.json'), key=lambda p: p.stat().st_mtime)
    roster = {str(house) for house, _ in json.loads(summaries[-1].read_text())['resolved_roster']} if summaries else set()
    owners = set()
    for house, item in re.findall(r'CAMPAIGN_(?:BUILDING|UNIT)_AUDIT: house=(\d+) (?:id=\d+ )?item=(\d+) ', log):
        if item in (CONSTRUCTION_YARD, MCV):
            owners.add(house)
    if len(roster) != players or not roster <= owners:
        problems.append(f'{entry["file"]}: lobby houses {sorted(roster)} (name says {players}), '
                        f'houses with a Construction Yard or MCV after one minute {sorted(owners)}')
    print(f'  {entry["name"]} ({entry["mod"]}): {len(roster & owners)}/{players} lobby houses hold their start, '
          + next(iter(re.findall(r'PROBE_DIGEST: .*final=1', log)), ''))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--build-dir', type=Path, help='Also load and play every map for one minute with this build')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    manifest = json.loads((root / 'data/maps/community-maps.json').read_text(encoding='utf-8'))
    problems = []
    for entry in manifest['maps']:
        checked = check_static(entry, problems)
        if checked and args.build_dir:
            check_game(entry, *checked, args.build_dir, args.output_dir or args.build_dir / 'community-maps', problems)
    for problem in problems:
        print('FAIL', problem)
    print(f'{len(manifest["maps"])} community maps, {len(problems)} problems')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
