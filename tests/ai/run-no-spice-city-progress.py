#!/usr/bin/env python3
"""Check banked city income after ordinary AI games, without fixture grants.

Every house that starts with a construction yard must bank more than 100 credits of city
tax. Every house still alive after the 12 minutes must also have built a four-lot R/I
opening. A house defeated meanwhile is reported (`defeated`) but not held to the lot count:
on the 3 vs 1 64x64 map Atreides loses its army to its Harkonnen neighbour's starting units
in minute 1-2 and its yard in minute 3-4, and whether it fits a third residential lot in
first is chance (docs/no-spice-economy-817.md, docs/quantbot-all-against-atreides.md).
"""
import argparse
import configparser
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import prune_runs  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--map', choices=('dunecity', 'low-cash'), required=True)
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
# Each CTest run gets a fresh directory; keep the two previous ones, not every run ever.
prune_runs(args.output_dir, args.map + '-', 2)
out = Path(tempfile.mkdtemp(prefix=args.map + '-', dir=args.output_dir))
source = root / 'data/maps/singleplayer' / (
    '4P - 192x192 - DuneCity.ini' if args.map == 'dunecity'
    else '4P - 64x64 - 3 vs 1.ini')
scenario = configparser.ConfigParser(strict=False, interpolation=None)
scenario.optionxform = str
scenario.read(source)
map_section = next(s for s in scenario.sections() if s.lower() == 'map')
for key, value in list(scenario[map_section].items()):
    if key.isdigit():
        scenario[map_section][key] = value.translate(str.maketrans('~+gGbrRBOQ', '-' * 10))
    elif key.lower() in ('field', 'bloom', 'special'):
        scenario.remove_option(map_section, key)
map_file = out / 'no-spice.ini'
with map_file.open('w') as f:
    scenario.write(f)
subprocess.run([
    'python3', str(root / 'tests/ai/run-campaign-balance.py'),
    '--build-dir', str(args.build_dir), '--output-dir', str(out / 'game'),
    '--custom-map', str(map_file), '--mod', 'dunecity', '--house', 'harkonnen',
    '--partner-difficulty', 'brutal', '--enemy-difficulty', 'brutal',
    '--concrete-required', '--harvester-limit', '0', '--seed', '148680876',
    '--minutes', '12', '--wall-timeout', '300',
], cwd=root, check=True, timeout=360)
files = list((out / 'game/profile/ai-decisions').glob('*/events.jsonl'))
assert len(files) == 1, 'Expected one isolated game capture'
initial, final = {}, None
for line in files[0].open():
    event = json.loads(line)
    data, house = event.get('data', {}), event.get('house', -1)
    if house >= 0 and data.get('state'):
        initial.setdefault(str(house), data['state'])
    if event['event'] == 'game_summary':
        final = data
assert final, 'Simulation must complete and flush cumulative economy totals'
results = {}
for house, state in initial.items():
    if state.get('construction_yards', 0) == 0:
        continue
    assert state['spice_remaining'] == 0, 'The engine found spice in the negative control'
    result = final['houses'][house]
    economy, built = result.get('economy_totals', {}), result.get('built', {})
    zones = [built.get(str(i), 0) for i in (20, 21, 22)]
    results[house] = {'starting_cash': state['credits'], 'zones_built': zones,
                      'final_cash': result['credits'], 'defeated': not result['alive'], **economy}
    assert economy.get('city_net_applied', 0) > 100, f'{house}: tax never became usable income'
    if result['alive']:
        assert sum(zones) >= 4 and zones[0] > 0 and zones[2] > 0, f'{house}: no funded R/I city opening'
assert results, 'No active AI construction yard was checked'
assert any(not r['defeated'] for r in results.values()), 'No surviving city was checked'
(out / 'income-receipt.json').write_text(json.dumps(results, indent=2) + '\n')
print('NO_SPICE_CITY_PROGRESS_OK', json.dumps(results, sort_keys=True))
