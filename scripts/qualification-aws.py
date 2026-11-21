#!/usr/bin/env python3
"""Mini qualification: the test games run on the AWS workers, the rest on the Mac.

The Mini builds the candidate and runs the quick tests that start no game; every test
that plays a game (PROCESSORS 2+, RUN_SERIAL, or a probe fixture) runs on the AWS
workers through `dunecity-queue` from the same full SHA. AWS results are merged into the
Mac JUnit before the unchanged receipt check, so production verifies one frozen file.
A pass that needed the queue's retry on another worker counts as a failure here: the
Mac run has no retries, and qualification must not be easier on AWS.

    split   --build build --sha SHA --state FILE   classify the frozen plan, write the Mac regex
    submit  --state FILE                           start the AWS job (inside the Mini gate)
    finish  --build build --state FILE             wait outside the gate, merge, check
    cancel  --state FILE                           stop the AWS job after a failed Mac lane
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PLAN = 'macos-headless-plan.json'
RESULTS = 'macos-headless-results.xml'
MAC_RESULTS = 'macos-headless-mac-results.xml'
AWS_RESULTS = 'aws-headless-results.xml'
MACHINES = ('aws', 'aws-mel2')
NAME = re.compile(r'[A-Za-z0-9_.\-]+\Z')
SHA = re.compile(r'[0-9a-f]{40}\Z')
# Qualification is short and gates staging; it goes ahead of research games.
PRIORITY = 10


def properties(test):
    return {item['name']: item['value'] for item in test.get('properties', [])}


def plays_a_game(props):
    """Single-game probes reserve two slots; whole-machine and fixture tests also run games."""
    serial = props.get('RUN_SERIAL') in (True, 'TRUE', 'ON', '1')
    return (serial or int(props.get('PROCESSORS', 1)) >= 2
            or bool(props.get('FIXTURES_REQUIRED')) or bool(props.get('FIXTURES_SETUP')))


def regex(names):
    if not names or any(not NAME.fullmatch(name) for name in names):
        raise ValueError('Test names must be nonempty plain identifiers')
    return '^(' + '|'.join(name.replace('.', r'\.') for name in sorted(names)) + ')$'


def split(build, sha):
    if not SHA.fullmatch(sha):
        raise ValueError('The AWS split needs the candidate full SHA')
    tests = json.loads((build / PLAN).read_text())['tests']
    games, quick = [], []
    for test in tests:
        (games if plays_a_game(properties(test)) else quick).append(test['name'])
    if not games or not quick or len(set(games + quick)) != len(tests):
        raise ValueError('The plan must contain both test games and quick tests, each named once')
    return dict(sha=sha, aws=sorted(games), mac=sorted(quick), job=None)


def save(path, state):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(state, indent=2, sort_keys=True) + '\n')
    os.replace(temporary, path)


def submit(state, run=subprocess.run, env=os.environ):
    if state.get('job'):
        raise ValueError('This attempt already submitted AWS job ' + state['job'])
    owner = 'github:{}:{}:qualify'.format(env['GITHUB_RUN_ID'], env['GITHUB_RUN_ATTEMPT'])
    command = ['dunecity-queue', 'ctest', '--cwd', env['GITHUB_WORKSPACE'], '--rev', state['sha'],
               '--machines', *MACHINES, '--priority', str(PRIORITY), '--purpose', 'verification',
               '--tag', 'qualify-' + env['GITHUB_RUN_ID'], '--owner', owner,
               '-R', regex(state['aws']), '-L', 'nightly', '-LE', 'network']
    result = run(command, stdout=subprocess.PIPE, text=True, timeout=600)
    lines = result.stdout.split()
    if result.returncode != 0 or not lines:
        raise ValueError('AWS test submission was refused (exit {})'.format(result.returncode))
    state['job'] = lines[-1]
    print('Test games for {} are AWS job {} ({} tests); the Mac runs {} quick tests.'.format(
        state['sha'], state['job'], len(state['aws']), len(state['mac'])), flush=True)
    return state


def cancel(state, run=subprocess.run):
    if state.get('job'):
        run(['dunecity-queue', 'cancel', state['job']], timeout=120)


def merge(build, state):
    """Append AWS testcases to the Mac JUnit, failing any that needed a retry."""
    mac = ET.parse(build / MAC_RESULTS)
    suite = mac.getroot()
    aws = ET.parse(build / AWS_RESULTS).getroot()
    facts = {item.get('name'): item.get('value') for item in aws.find('properties')}
    if facts.get('queue_job') != state['job'] or facts.get('source_commit') != state['sha']:
        raise ValueError('AWS results belong to a different job or source')
    if facts.get('queue_state') not in ('done', 'failed'):
        raise ValueError('AWS job did not finish: ' + str(facts.get('queue_state')))
    mac_names = sorted(case.get('name') for case in suite.iter('testcase'))
    if mac_names != state['mac']:
        raise ValueError('The Mac ran a different set of quick tests than the plan split selected')
    cases = list(aws.iter('testcase'))
    if sorted(case.get('name') for case in cases) != state['aws']:
        raise ValueError('AWS ran a different set of test games than the plan split selected')
    failures = 0
    for case in cases:
        props = case.find('properties')
        attempts = next(item.get('value') for item in props if item.get('name') == 'attempts')
        for name, value in (('queue_job', state['job']), ('source_commit', state['sha'])):
            ET.SubElement(props, 'property', name=name, value=value)
        statuses = [part.rpartition(':')[2] for part in attempts.split(';') if part]
        if case.get('status') == 'run' and (not statuses or any(status != 'Passed' for status in statuses)):
            case.set('status', 'fail')
            ET.SubElement(case, 'failure', message='passed only on a retry: ' + attempts)
        failures += case.get('status') != 'run'
        suite.append(case)
    suite.set('tests', str(len(state['mac']) + len(cases)))
    suite.set('failures', str(int(suite.get('failures', '0')) + failures))
    temporary = build / (RESULTS + '.tmp')
    mac.write(temporary, encoding='UTF-8', xml_declaration=True)
    os.replace(temporary, build / RESULTS)
    return failures


def finish(build, state, timeout, run=subprocess.run, clock=time.monotonic):
    if not state.get('job'):
        raise ValueError('No AWS job was submitted for this attempt')
    started = clock()
    try:
        run(['dunecity-queue', 'wait', state['job']], timeout=timeout)
    except subprocess.TimeoutExpired:
        cancel(state, run)
        raise ValueError('AWS job {} did not finish within {} minutes; cancelled'.format(state['job'], timeout // 60))
    print('AWS job {} finished after {:.1f} minutes'.format(state['job'], (clock() - started) / 60), flush=True)
    written = run(['dunecity-queue', 'report', state['job'], '--junit', str(build / AWS_RESULTS)], timeout=300)
    if written.returncode not in (0, 1):
        raise ValueError('AWS JUnit was not written (exit {})'.format(written.returncode))
    failures = merge(build, state)
    mac_failures = sum(1 for case in ET.parse(build / RESULTS).getroot().iter('testcase')
                       if case.get('status') != 'run')
    # CTest exit 8 is "tests failed"; only committed known-failure decisions can accept them.
    status = 8 if mac_failures else 0
    check = run([sys.executable, str(ROOT / 'scripts/release-automation.py'), 'check-headless',
                 '--directory', str(build), '--ctest-status', str(status)])
    if check.returncode != 0:
        raise ValueError('Qualification failed: {} AWS and {} total test failures'.format(failures, mac_failures))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('split', 'submit', 'finish', 'cancel'))
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--sha')
    parser.add_argument('--timeout-minutes', type=int, default=40)
    args = parser.parse_args(argv)
    if args.command == 'split':
        state = split(args.build, args.sha or '')
        save(args.state, state)
        print(regex(state['mac']))
        return 0
    state = json.loads(args.state.read_text()) if args.state.exists() else {}
    if args.command == 'cancel':
        cancel(state)
    elif args.command == 'submit':
        save(args.state, submit(state))
    else:
        finish(args.build, state, args.timeout_minutes * 60)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except ValueError as error:
        print('::error::' + str(error), file=sys.stderr)
        sys.exit(1)
