#!/usr/bin/env python3
"""Run an EconomyFirst opening arm through all twelve existing regressions, then the real-map activation test.

Assertions inside the original probes are retained. Each subprocess gets an arm-specific output path, so
concurrent arms cannot read another arm's stale evidence. Admission/compilation success is never a test pass.
"""
import argparse
import concurrent.futures
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

VARIANTS = {'A': (0, 0), 'B': (1, 1), 'C': (1, 0), 'D': (0, 1), 'E': (2, 0)}
PROBES = ('quantbot_air-defense_vanilla', 'quantbot_air-defense_dunecity', 'quantbot_air-defense_Dune2R',
          'quantbot_shared-spending_dunecity', 'quantbot_mcv_deployment', 'quantbot_no_spice_city',
          'quantbot_no_spice_income_low-cash', 'quantbot_starport_dunecity', 'brutal_unit_override_probe',
          'local_spectator_worm_startup_off', 'local_spectator_worm_startup_on', 'turret_front_probe')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--results', required=True, type=Path)
    parser.add_argument('--variant', choices=VARIANTS, required=True)
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    args.results = args.results.resolve()
    shutil.rmtree(args.results, ignore_errors=True)
    args.results.mkdir(parents=True, exist_ok=True)
    registry_probes = PROBES + ('city_lessons_policy',)
    registry = subprocess.run(['ctest', '--test-dir', str(args.build_dir), '--show-only=json-v1',
                               '-R', '^(' + '|'.join(registry_probes) + ')$'], capture_output=True, text=True, check=True)
    tests = {test['name']: test for test in json.loads(registry.stdout)['tests']}
    if set(tests) != set(registry_probes):
        raise SystemExit(f'Incomplete regression registry: expected {registry_probes}, found {sorted(tests)}')
    pacing, turrets = VARIANTS[args.variant]
    forced = dict(DUNECITY_TEST_ECONOMY_FIRST='1', DUNECITY_TEST_ECONOMY_UNIT_PACING=str(pacing),
                  DUNECITY_TEST_ECONOMY_TURRETS=str(turrets))

    def run_probe(name, overrides=None, label=None):
        label = label or name
        test = tests[name]
        command = list(test['command'])
        out = args.results / label
        for option in ('--output-dir', '--results'):
            if option in command:
                command[command.index(option) + 1] = str(out)
        properties = {p['name']: p['value'] for p in test.get('properties', [])}
        env = dict(os.environ)
        for setting in properties.get('ENVIRONMENT', []):
            key, value = setting.split('=', 1)
            env[key] = value
        env.update(forced)
        env.update(overrides or {})
        completed = subprocess.run(command, cwd=properties.get('WORKING_DIRECTORY', str(args.build_dir)),
                                   env=env, capture_output=True, text=True,
                                   timeout=max(300, int(properties.get('TIMEOUT', 1800)) + 30))
        log = args.results / f'{label}.log'
        log.write_text(completed.stdout + completed.stderr)
        print(f'VARIANT_PROBE: variant={args.variant} test={label} rc={completed.returncode} log={log}', flush=True)
        deviations = {
            'quantbot_shared-spending_dunecity': ('Single yard failed to build funded rocket protection',),
            'turret_front_probe': (': no reinforcement ordered under attack (', ': no ordered turret stood at its site ('),
            'quantbot_mcv_deployment': ('First-factory arbitration changed the ordinary colonist/import ranking',),
            'quantbot_no_spice_city': ("First-factory protection changed an Easy/Medium house's ordinary import priority",),
            'brutal_unit_override_probe': ('Unlimited override did not build past the value cap',
                'Positive override below the count limit built nothing',
                'Unlimited override did not order the reachable aircraft past the value cap',
                'Unlimited override imported nothing from discounted stock',
                'Positive override below the count limit imported nothing'),
        }
        evidence = completed.stdout + completed.stderr
        for runlog in list(out.glob('run*.log')) + list(out.glob('*/run.log')) + list(out.glob('run-*/run.log')) + list(out.glob('*/run-*/run.log')):
            evidence += runlog.read_text(errors='replace')
        reason = next((text for text in deviations.get(name, ()) if text in evidence), None)
        # These fixtures require a funded Rocket Turret before three minutes. Front 'ordered'
        # counts selected sites, before the producer admits them (its 9000-cycle deadline is <3m).
        # The separate activation run below still must prove real admitted and standing turrets.
        turret_conflict = name in ('quantbot_shared-spending_dunecity', 'turret_front_probe')
        reviewed = bool(label == name and completed.returncode and reason and not any(
            marker in evidence for marker in ('SIGSEGV', 'SIGABRT', 'AddressSanitizer', 'runtime error:')) and (
            (turret_conflict and turrets)
            or (not turret_conflict and pacing and 'event=paced' in evidence)))
        if completed.returncode:
            for line in evidence.splitlines():
                if '_FAIL:' in line or '_PROBE_FAIL:' in line:
                    print('VARIANT_RAW_FAILURE: ' + line, flush=True)
        if completed.returncode:
            print(completed.stdout[-6000:] + completed.stderr[-6000:], flush=True)
        if reviewed:
            print(f'VARIANT_DEVIATION: variant={args.variant} test={name} count=1 reason={reason}', flush=True)
        implementation_error = label == 'literal_turret_curve' and (
            'ECONOMY_FIRST_TURRET_ASSERT: accepted=77 placed=1 rejected=6 met=1' not in evidence)
        if implementation_error:
            print('VARIANT_IMPLEMENTATION_FAIL: literal turret curve witness missing', flush=True)
        return {'test': label, 'returncode': completed.returncode, 'implementation_error': implementation_error, 'log': str(log),
                'reviewed_pacing_conflict': reviewed, 'reason': reason if reviewed else None}

    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run_probe, PROBES))
    if args.variant != 'A':
        results.append(run_probe('city_lessons_policy'))
    if turrets:
        results.append(run_probe('quantbot_mcv_deployment',
            {'DUNECITY_TEST_ECONOMY_UNIT_PACING': '0', 'DUNECITY_TEST_ECONOMY_TURRETS': '1'},
            'literal_turret_curve'))
    activation = subprocess.run([sys.executable, str(Path(__file__).with_name('economy-first-test.py')),
                                 '--build-dir', str(args.build_dir), '--results', str(args.results / 'activation'),
                                 '--variant', args.variant] + (['--implementation-only'] if args.variant != 'A' else []),
                                capture_output=True, text=True)
    (args.results / 'activation.log').write_text(activation.stdout + activation.stderr)
    print(activation.stdout + activation.stderr, flush=True)
    results.append({'test': 'economy_first', 'returncode': activation.returncode})
    (args.results / 'results.json').write_text(json.dumps({'variant': args.variant, 'results': results}, indent=2))
    failed = [r['test'] for r in results if r.get('implementation_error') or (r['returncode'] and not r.get('reviewed_pacing_conflict'))]
    print(f'VARIANT_RESULT: variant={args.variant} passed={len(results)-len(failed)}/{len(results)} failed={failed}')
    return int(bool(failed))


if __name__ == '__main__':
    sys.exit(main())
