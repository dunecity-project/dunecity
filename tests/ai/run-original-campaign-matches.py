#!/usr/bin/env python3
"""Original campaign mission smoke and same-binary unaffected-controller controls."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', required=True, type=Path)
parser.add_argument('--output-dir', required=True, type=Path)
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
cases = []
for house in ('harkonnen', 'atreides', 'ordos'):
    for level in (1, 4, 9):
        cases.append((f'{house}-{level}-original', house, level, 'campaign', 'on', 'vanilla'))
    cases.append((f'{house}-4-legacy', house, 4, 'campaign', 'off', 'vanilla'))
    for rules in ('off', 'on'):
        cases.append((f'{house}-4-quantbot-{rules}', house, 4, 'quantbot', rules, 'vanilla'))
    for mod in ('dunecity', 'Tornie'):
        cases.append((f'{house}-4-{mod}', house, 4, 'campaign', 'on', mod))
cases.append(('harkonnen-9-repeat', 'harkonnen', 9, 'campaign', 'on', 'vanilla'))

def run(case):
    name, house, level, controller, rules, mod = case
    out = args.output_dir / name
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
               '--build-dir', str(args.build_dir), '--output-dir', str(out),
               '--scenario', '--repeatable', '--headless-assets', '--no-ai-telemetry',
               '--house', house, '--level', str(level), '--mod', mod,
               '--partner-difficulty', 'brutal', '--enemy-ai', controller,
               '--original-campaign-rules', rules, '--seed', '486409243', '--minutes', '15']
    log = args.output_dir / f'{name}.log'
    previous = set(out.glob('run-*/run.log'))
    try:
        with log.open('w') as handle:
            subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, check=True, timeout=240)
        runs = list(set(out.glob('run-*/run.log')) - previous)
        if len(runs) != 1:
            raise RuntimeError(f'Expected one run log, got {len(runs)}; use fresh output directory')
        text = runs[0].read_text(errors='replace')
        digest = re.findall(r'PROBE_DIGEST:.*final=1[^\n]*', text)
        if len(digest) != 1:
            raise RuntimeError('Missing final engine digest')
        roster = re.findall(r'ORIGINAL_CAMPAIGN_ROSTER: house=(\d+) original=([01])', text)
        if controller == 'campaign':
            if not roster or any(mode != str(int(rules == 'on')) for _, mode in roster):
                raise RuntimeError(f'Wrong Campaign AI mode: {roster}')
        elif roster:
            raise RuntimeError('QuantBot control unexpectedly contains Campaign AI')
        # Compare only simulation digest, not wall time or timing telemetry.
        value = re.search(r'\bdigest=(\S+)', digest[0])
        if not value:
            raise RuntimeError('Missing digest value')
        return dict(name=name, pass_=True, digest=value[1], roster=roster)
    except Exception as error:
        return dict(name=name, pass_=False, error=str(error), log=str(log))

with ThreadPoolExecutor(max_workers=3) as pool:
    results = list(pool.map(run, cases))
by_name = {row['name']: row for row in results}
failures = [row for row in results if not row['pass_']]
for house in ('harkonnen', 'atreides', 'ordos'):
    left, right = (by_name[f'{house}-4-quantbot-{arm}'] for arm in ('off', 'on'))
    if left.get('digest') != right.get('digest'):
        failures.append(dict(name=f'{house}-quantbot-inertness', error='OFF/ON digests differ'))
if by_name['harkonnen-9-original'].get('digest') != by_name['harkonnen-9-repeat'].get('digest'):
    failures.append(dict(name='original-repeatability', error='Identical original campaign replay diverged'))
(args.output_dir / 'results.json').write_text(json.dumps(dict(cases=results, failures=failures), indent=2) + '\n')
for failure in failures:
    print(failure)
print(f'Original campaign matches: {len(results) - sum(not row["pass_"] for row in results)}/{len(results)} executed; '
      f'{len(failures)} failures including parity checks')
sys.exit(bool(failures))
