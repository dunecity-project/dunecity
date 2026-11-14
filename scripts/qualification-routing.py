#!/usr/bin/env python3
"""Bounded Mini queue fallback using only the current job's Actions token.

No runner-administration credential or persistent host dispatcher is needed.
Actual build/test failures are handled by the workflow's admission output and
never retried here. The queue watcher stops as soon as the runner starts.
"""
import json
import os
import re
import subprocess
import sys
import time

REPOSITORY = 'dunecity-project/dunecity'
WORKFLOW = 'qualify-release.yml'
QUEUE_SECONDS = 120


def command(*args):
    return subprocess.check_output(args, text=True, timeout=30)


def context(env):
    sha, ref = env['CANDIDATE_SHA'], env['GITHUB_REF_NAME']
    if (env['GITHUB_REPOSITORY'] != REPOSITORY or not re.fullmatch('[0-9a-f]{40}', sha)
            or ref not in ('main', 'integrate/feature-batch')
            or not re.fullmatch('[1-9][0-9]*', env['GITHUB_RUN_ID'])
            or not re.fullmatch('[1-9][0-9]*', env['GITHUB_RUN_ATTEMPT'])):
        raise ValueError('Fallback requires a trusted control branch, candidate SHA and run attempt')
    return sha, ref, env['GITHUB_RUN_ID'], env['GITHUB_RUN_ATTEMPT']


def dispatch(sha, ref, call=command):
    # The hosted input is terminal: that run cannot dispatch another fallback.
    call('gh', 'workflow', 'run', WORKFLOW, '--repo', REPOSITORY, '--ref', ref,
         '-f', 'sha=' + sha, '-f', 'backend=hosted')
    print('Hosted qualification dispatched for ' + sha, flush=True)


def refused(env, call=command):
    sha, ref, run, attempt = context(env)
    data = json.loads(call('gh', 'api', f'repos/{REPOSITORY}/actions/runs/{run}/attempts/{attempt}/jobs?per_page=100'))
    if data.get('total_count') != len(data.get('jobs', [])) or data['total_count'] > 100:
        raise ValueError('Mini refusal status is incomplete')
    matches = [job for job in data['jobs'] if job['name'] == 'qualify']
    if len(matches) != 1 or matches[0].get('conclusion') != 'failure':
        raise ValueError('Mini job did not fail admission')
    steps = {step['name']: step for step in matches[0].get('steps', [])}
    lane = steps.get('Qualify frozen staging source before production selection', {})
    marker = steps.get('Report whether Mini qualification actually started', {})
    # Explicit marker absence, successfully observed after the step, proves the
    # lane never began. With no report, only a never-started lane is safe to retry.
    safe = (env.get('MINI_ADMITTED') == 'false' and marker.get('conclusion') == 'success')
    safe |= (not env.get('MINI_ADMITTED') and lane.get('status') != 'in_progress'
             and lane.get('conclusion') in (None, 'skipped') and not lane.get('started_at'))
    if not safe:
        raise ValueError('Mini work may have started; preserve failure without automatic retry')
    dispatch(sha, ref, call)


def watch(env, call=command, clock=time.monotonic, sleep=time.sleep):
    sha, ref, run, attempt = context(env)
    endpoint = f'repos/{REPOSITORY}/actions/runs/{run}/attempts/{attempt}/jobs?per_page=100'
    deadline = clock() + QUEUE_SECONDS
    while True:
        data = json.loads(call('gh', 'api', endpoint))
        if data.get('total_count') != len(data.get('jobs', [])) or data['total_count'] > 100:
            raise ValueError('Mini queue status is incomplete')
        matches = [job for job in data['jobs'] if job['name'] == 'qualify']
        if len(matches) > 1:
            raise ValueError('Mini queue status is ambiguous')
        if matches and matches[0]['status'] in ('in_progress', 'completed'):
            print('Mini job started or finished; queue fallback not needed', flush=True)
            return
        if matches and matches[0]['status'] != 'queued':
            raise ValueError('Unknown Mini job state')
        if clock() >= deadline:
            # Dispatch first, then cancel this unstarted run. If assignment races
            # with cancellation, the owned queue child is terminated by its gate.
            # A failed cancel is surfaced, never treated as confirmed cancellation.
            dispatch(sha, ref, call)
            call('gh', 'api', '--method', 'POST', f'repos/{REPOSITORY}/actions/runs/{run}/cancel')
            return
        sleep(min(15, max(0, deadline - clock())))


if __name__ == '__main__':
    if sys.argv[1:] == ['wait-for-mini']:
        watch(os.environ)
    elif sys.argv[1:] == ['refused-mini']:
        refused(os.environ)
    else:
        raise SystemExit('Expected wait-for-mini or refused-mini')
