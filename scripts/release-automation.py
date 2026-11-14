#!/usr/bin/env python3
"""Select staging inputs, publish immutable packages, and verify/resume delivery.

Recovery never invokes a compiler:
  python3 scripts/release-automation.py verify v1.0.865 --live
  python3 scripts/release-automation.py resume browser --tag v1.0.865
  python3 scripts/release-automation.py resume sourceforge --tag v1.0.865
  python3 scripts/release-automation.py resume deploy --run-id WEBSITE_RUN_ID
"""
import argparse
import base64
import datetime as dt
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
STAGING = 'integrate/feature-batch'
RECIPE = 'windows-2022-linux-22.04-macos-15-arm64-wasm-4.0.14-dev-v1'
BROWSER = 'DuneCity-browser.tar.gz'
MANIFEST = 'publication-inputs.json'
HEADLESS_PLAN = 'macos-headless-plan.json'
HEADLESS_RESULTS = 'macos-headless-results.xml'
HEADLESS_RECEIPT = 'macos-headless-qualification.json'
QUALIFY_WORKFLOW = '.github/workflows/qualify-release.yml'
QUALIFY_ARTIFACT = 'DuneCity-macOS-headless-qualification'
QUALIFY_FILES = (HEADLESS_PLAN, HEADLESS_RESULTS, HEADLESS_RECEIPT)
EXCEPTIONS = 'releases/qualification-exceptions.json'
# Conservative dependency scope: an exception cannot survive a changed game,
# test, dependency recipe or harness. No inference of equivalence from filenames.
EXCEPTION_INPUTS = ('src', 'include', 'tests', 'cmake', 'CMakeLists.txt', 'vcpkg.json',
                    'vcpkg-configuration.json', 'scripts', '.github/workflows', 'data', 'mods',
                    'config', 'tools', 'platform', 'IDE', 'imported_sprites', 'web')
STABLE = re.compile(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
SHA = re.compile(r'[0-9a-f]{40}\Z')
NS = '{http://www.andymatuschak.org/xml-namespaces/sparkle}'
STAGING_TAG = re.compile(r'staging-([0-9a-f]{40})-([0-9a-f]{12})\Z')
STAGING_POINTER = re.compile(r'<!--\s*dunecity-staging-tag:\s*(staging-[0-9a-f]{40}-[0-9a-f]{12})\s*-->')
POINTER_BEGIN = '<!-- dunecity-staging-pointer-begin -->'
POINTER_END = '<!-- dunecity-staging-pointer-end -->'
IOS_CRON = '23 4 * * 1'
BUILD_WORKFLOW = '.github/workflows/build.yml'
CI_JOBS = frozenset(('test-linux', 'relay-tests', 'signaling-tests', 'build-emscripten',
                     'build-windows', 'build-linux', 'build-macos'))


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def repository():
    return os.environ.get('GITHUB_REPOSITORY', 'dunecity-project/dunecity')


def api(path):
    return json.loads(run('gh', 'api', path))


def optional_release(tag):
    result = subprocess.run(['gh', 'api', f'repos/{repository()}/releases/tags/{tag}'],
                            capture_output=True, text=True)
    if result.returncode == 0:
        release = json.loads(result.stdout)
        if release['tag_name'] != tag:
            raise ValueError('Release lookup returned a different tag')
        return release
    if 'HTTP 404' not in result.stderr:
        raise RuntimeError(result.stderr.strip())
    # The tag endpoint only returns published releases. Drafts need a complete
    # listing: stopping at the first match could silently choose a duplicate.
    matches = []
    for page in range(1, 1001):
        batch = api(f'repos/{repository()}/releases?per_page=100&page={page}')
        if not isinstance(batch, list):
            raise ValueError('Release listing is incomplete')
        matches.extend(item for item in batch if item['tag_name'] == tag)
        if len(batch) < 100:
            break
    else:
        raise ValueError('Release pagination is incomplete')
    if len(matches) > 1:
        raise ValueError('Ambiguous release tag: ' + tag)
    if matches and not matches[0]['draft']:
        raise ValueError('Published release tag lookup disagrees with release listing')
    return matches[0] if matches else None


def require_unretired_staging(sha, release):
    if release is not None:
        return
    tag = snapshot_tag(sha)
    result = subprocess.run(['gh', 'api', f'repos/{repository()}/git/ref/tags/{tag}'],
                            capture_output=True, text=True)
    if result.returncode == 0:
        if json.loads(result.stdout)['ref'] != 'refs/tags/' + tag:
            raise ValueError('Ambiguous immutable staging tag state')
        raise ValueError('Retired staging snapshot: ' + tag + '; choose a new source, never rebuild its immutable inputs')
    if 'HTTP 404' not in result.stderr:
        raise RuntimeError('Cannot establish staging snapshot retirement state: ' + result.stderr.strip())


def resolve_tag(tag):
    obj = api(f'repos/{repository()}/git/ref/tags/{tag}')['object']
    while obj['type'] == 'tag':
        obj = api(f'repos/{repository()}/git/tags/{obj["sha"]}')['object']
    if obj['type'] != 'commit':
        raise ValueError('Publication tag does not identify a source commit')
    return full_sha(obj['sha'])


def ensure_staging_tag(tag, sha):
    """Create only the expected immutable staging ref; never move any existing ref."""
    if tag != snapshot_tag(full_sha(sha)):
        raise ValueError('Only the expected immutable staging tag may be created')
    result = subprocess.run(['gh', 'api', f'repos/{repository()}/git/ref/tags/{tag}'],
                            capture_output=True, text=True)
    if result.returncode == 0:
        if json.loads(result.stdout)['ref'] != 'refs/tags/' + tag:
            raise ValueError('Ambiguous immutable staging tag state')
    elif 'HTTP 404' in result.stderr:
        subprocess.run(['gh', 'api', '--method', 'POST', f'repos/{repository()}/git/refs',
                        '-f', 'ref=refs/tags/' + tag, '-f', 'sha=' + sha], check=True)
    else:
        raise RuntimeError('Cannot establish immutable staging tag state: ' + result.stderr.strip())
    if resolve_tag(tag) != sha:
        raise ValueError('Immutable staging tag does not point at the selected source')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def full_sha(value):
    if not SHA.fullmatch(value):
        raise ValueError('An exact lowercase full 40-character source SHA is required')
    return value


def snapshot_tag(sha, recipe=RECIPE):
    full_sha(sha)
    identity = hashlib.sha256((sha + '\n' + recipe).encode()).hexdigest()
    return f'staging-{sha}-{identity[:12]}'


def iso_time(value):
    return dt.datetime.fromisoformat(value.replace('Z', '+00:00'))


def package_decision(sha, green, existing, runs, now, manual=False, current_run=None):
    """Never substitute an older green head or rebuild an existing immutable set."""
    full_sha(sha)
    if existing:
        if existing['draft']:
            return False, 'existing draft snapshot: resume its producing workflow; do not rebuild identical inputs'
        return False, 'no-new-inputs: ' + snapshot_tag(sha)
    if not green:
        return False, 'staging head has no successful integration CI: ' + sha
    for item in runs:
        if str(item['id']) == str(current_run):
            continue
        if item.get('display_title', '').startswith('Staging packages'):
            if not manual and now - iso_time(item['created_at']) < dt.timedelta(hours=2):
                return False, f'two-hour debounce: producing run {item["id"]}'
    return True, 'green staging snapshot selected'


def emit(values):
    text = ''.join(f'{key}={str(value).lower() if isinstance(value, bool) else value}\n'
                   for key, value in values.items())
    print(text, end='')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
            stream.write(text)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write('### Selected inputs\n```\n' + text + '```\n')


def workflow_runs():
    return api(f'repos/{repository()}/actions/workflows/build.yml/runs?per_page=100')['workflow_runs']


def docs_only(paths):
    # GitHub compare caps its changed-file list at 300: an ambiguous list must build.
    return bool(paths) and len(paths) < 300 and all(path.startswith(('docs/', 'releases/desktop/')) or
                               path in ('README.md', 'HANDOVER.md', 'CHANGELOG.md', 'LICENSE')
                               for path in paths)


def integration_green(source_run, jobs, sha):
    required = CI_JOBS
    return (source_run['head_sha'] == sha and source_run['head_branch'] == STAGING
            and source_run['event'] == 'push'
            and required.issubset({job['name'] for job in jobs if job['conclusion'] == 'success'}))


def qualification_for(sha, read=None):
    """Find successful source CI, independently of the packaging control revision."""
    read = read or api
    full_sha(sha)
    branch = urllib.parse.quote(STAGING, safe='')
    runs = read(f'repos/{repository()}/actions/workflows/build.yml/runs?branch={branch}&head_sha={sha}&event=push&status=success&per_page=100')['workflow_runs']
    for candidate in runs:
        if (candidate['status'] != 'completed' or candidate['conclusion'] != 'success'
                or candidate['path'] != BUILD_WORKFLOW):
            continue
        jobs = read(f'repos/{repository()}/actions/runs/{candidate["id"]}/attempts/{candidate["run_attempt"]}/jobs?per_page=100')['jobs']
        if integration_green(candidate, jobs, sha):
            return candidate
    raise ValueError('No completed successful integration qualification for source ' + sha)


def validate_staging_receipts(manifest, publication=None):
    """Validate immutable API receipts; package producer HEAD need not be game source."""
    sha = full_sha(manifest['sourceCommit'])
    fields = {'qualificationRunId', 'qualificationRunAttempt', 'qualificationSourceCommit',
              'producerSourceCommit', 'producerEvent', 'producerHeadBranch', 'producerWorkflowPath'}
    if not fields.issubset(manifest):
        raise ValueError('Staging snapshot lacks explicit source qualification/producer receipts')
    if manifest['qualificationSourceCommit'] != sha:
        raise ValueError('Integration qualification source does not match the snapshot')
    qid, attempt = int(manifest['qualificationRunId']), int(manifest['qualificationRunAttempt'])
    if qid <= 0 or attempt <= 0:
        raise ValueError('Invalid integration qualification run/attempt')
    qualified = api(f'repos/{repository()}/actions/runs/{qid}/attempts/{attempt}')
    qjobs = api(f'repos/{repository()}/actions/runs/{qid}/attempts/{attempt}/jobs?per_page=100')['jobs']
    if (qualified['status'] != 'completed' or qualified['conclusion'] != 'success'
            or qualified['path'] != BUILD_WORKFLOW or qualified['run_attempt'] != attempt
            or qualified['repository']['full_name'] != repository()
            or not integration_green(qualified, qjobs, sha)):
        raise ValueError('Source integration qualification is not independently successful')
    pid, pattempt = int(manifest['runId']), int(manifest['runAttempt'])
    if pid <= 0 or pattempt <= 0:
        raise ValueError('Invalid package producer run/attempt')
    live_inputs = (publication is not None and publication['id'] == pid
                   and publication['run_attempt'] == pattempt)
    producer = api(f'repos/{repository()}/actions/runs/{pid}/attempts/{pattempt}')
    pjobs = api(f'repos/{repository()}/actions/runs/{pid}/attempts/{pattempt}/jobs?per_page=100')['jobs']
    if ((producer['status'] != 'completed' and not
         (live_inputs and producer['status'] == 'in_progress' and producer['conclusion'] is None))
            or producer['id'] != pid
            or producer['path'] != BUILD_WORKFLOW or producer['run_attempt'] != pattempt
            or producer['repository']['full_name'] != repository()
            or producer['event'] not in ('schedule', 'workflow_dispatch')
            or producer['head_branch'] not in ('main', STAGING)
            or (producer['event'] == 'schedule' and producer['head_branch'] != 'main')
            or producer['head_sha'] != manifest['producerSourceCommit']
            or producer['event'] != manifest['producerEvent']
            or producer['head_branch'] != manifest['producerHeadBranch']
            or producer['path'] != manifest['producerWorkflowPath']):
        raise ValueError('Package producer is not independently successful/trusted')
    outcomes = {name: [job['conclusion'] for job in pjobs if job['name'] == name]
                for name in CI_JOBS | {'dev-release'}}
    if any(outcomes[name] != ['success'] for name in CI_JOBS):
        raise ValueError('Package producer input jobs did not all succeed at the frozen attempt')
    if live_inputs:
        live = [job for job in pjobs if job['name'] == 'dev-release']
        if (producer['status'] != 'in_progress' or producer['conclusion'] is not None
                or len(live) != 1 or live[0]['status'] != 'in_progress' or live[0]['conclusion'] is not None):
            raise ValueError('Current package publication job is not independently in progress')
        return
    if producer['conclusion'] == 'success' and outcomes['dev-release'] == ['success']:
        return
    if producer['conclusion'] != 'failure' or outcomes['dev-release'] != ['failure']:
        raise ValueError('Package producer has no successful publication receipt')
    # runAttempt identifies the package inputs, not a later publication retry.
    # Preserve it even when only dev-release was rerun with inherited successes.
    publication_run = publication or api(f'repos/{repository()}/actions/runs/{pid}')
    live_retry = publication is not None
    if (publication_run['id'] != pid or publication_run['run_attempt'] <= pattempt
            or publication_run['status'] != ('in_progress' if live_retry else 'completed')
            or publication_run['conclusion'] != (None if live_retry else 'success')
            or publication_run['repository']['full_name'] != repository()
            or any(publication_run[key] != producer[key] for key in
                   ('path', 'head_sha', 'head_branch', 'event'))):
        raise ValueError('Package producer publication retry is not independently successful/trusted')
    retry = publication_run['run_attempt']
    jobs = api(f'repos/{repository()}/actions/runs/{pid}/attempts/{retry}/jobs?per_page=100')['jobs']
    live = [job for job in jobs if job['name'] == 'dev-release']
    if ((live_retry and (len(live) != 1 or live[0]['status'] != 'in_progress' or live[0]['conclusion'] is not None))
            or (not live_retry and [job['conclusion'] for job in live] != ['success'])
            or any(job['name'] in CI_JOBS and job['conclusion'] != 'success' for job in jobs)):
        raise ValueError('Package producer publication retry contains failed/skipped required jobs')


def qualified_staging_snapshot(tag, expected_sha=None, publication=None):
    match = STAGING_TAG.fullmatch(tag)
    if not match:
        raise ValueError('Staging requires an immutable qualified staging tag')
    sha = match[1]
    if expected_sha is not None and sha != expected_sha:
        raise ValueError('Staging source must exactly match the selected source commit')
    release = optional_release(tag)
    if not release or release['draft'] or not release['prerelease']:
        raise ValueError('Staging snapshot is not a published prerelease')
    if resolve_tag(tag) != sha:
        raise ValueError('Staging tag does not identify its advertised source')
    with tempfile.TemporaryDirectory(prefix='dunecity-staging-inputs-') as temporary:
        folder = Path(temporary)
        download_release(tag, folder, [MANIFEST])
        manifest = json.loads((folder / MANIFEST).read_text())
    identity = hashlib.sha256((sha + '\n' + manifest['recipe']).encode()).hexdigest()
    if (manifest['schema'] != 1 or manifest['tag'] != tag or manifest['sourceCommit'] != sha
            or manifest['repository'] != repository() or manifest['releaseBuild'] != 'OFF'
            or tag != snapshot_tag(sha, manifest['recipe']) or manifest['inputIdentity'] != identity):
        raise ValueError('Published input manifest does not match the staging snapshot')
    required = desktop_names(manifest['version']) | {BROWSER, 'DuneCity-p2p-service.tar.gz'}
    required |= {f'build-inputs-{platform}.json' for platform in
                 ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten')}
    if not isinstance(manifest.get('sha256'), dict) or not required.issubset(manifest['sha256']):
        raise ValueError('Qualified snapshot has an incomplete required platform/input set')
    assets = {item['name']: item for item in release['assets']}
    if len(assets) != len(release['assets']) or set(assets) != set(manifest['sha256']) | {MANIFEST, 'SHA256SUMS'}:
        raise ValueError('Qualified snapshot published asset set differs from its manifest')
    for name, checksum in manifest['sha256'].items():
        item = assets.get(name, {})
        if (Path(name).name != name or not isinstance(checksum, str) or not re.fullmatch(r'[0-9a-f]{64}', checksum)
                or item.get('size', 0) <= 0 or item.get('digest') != 'sha256:' + checksum):
            raise ValueError('Qualified snapshot published asset is missing/corrupt: ' + name)
    sums = ''.join(f'{checksum}  {name}\n' for name, checksum in sorted(manifest['sha256'].items())).encode()
    if (assets['SHA256SUMS'].get('size') != len(sums)
            or assets['SHA256SUMS'].get('digest') != 'sha256:' + hashlib.sha256(sums).hexdigest()):
        raise ValueError('Qualified snapshot published checksums do not match its manifest')
    validate_staging_receipts(manifest, publication)
    head = api(f'repos/{repository()}/git/ref/heads/{STAGING}')['object']['sha']
    if sha != head and api(f'repos/{repository()}/compare/{sha}...{head}')['status'] not in ('ahead', 'identical'):
        raise ValueError('Staging source is outside trusted integration history')
    return manifest, head


def ready_ios_artifact(sha):
    # The API exposes at most 1,000 filtered runs. Do not rebuild merely because
    # an unchanged-source reuse receipt is on a later page or history is truncated.
    for page in range(1, 11):
        data = api(f'repos/{repository()}/actions/workflows/build.yml/runs?head_sha={sha}&event=workflow_dispatch&status=success&per_page=100&page={page}')
        runs = data['workflow_runs']
        for source in runs:
            if (source['head_sha'] != sha or source['event'] != 'workflow_dispatch'
                    or source['path'] != BUILD_WORKFLOW or not STAGING_TAG.fullmatch(source['head_branch'])
                    or source['status'] != 'completed' or source['conclusion'] != 'success'
                    or not source['display_title'].startswith('iOS internal staging ')
                    or str(source['id']) == os.environ.get('GITHUB_RUN_ID')):
                continue
            artifacts = api(f'repos/{repository()}/actions/runs/{source["id"]}/artifacts?per_page=100')['artifacts']
            for artifact in artifacts:
                if (artifact['name'].startswith(f'DuneCity-iOS-internal-{sha}-')
                        and not artifact['expired'] and artifact['size_in_bytes'] > 0):
                    if not re.fullmatch(r'sha256:[0-9a-f]{64}', artifact.get('digest') or ''):
                        raise ValueError('Existing internal iOS artifact has no authoritative digest; inspect before rebuilding')
                    return source['id'], artifact['id']
        if len(runs) < 100:
            return None
        if page == 10 and data['total_count'] > 1000:
            raise ValueError('iOS reuse history exceeds the API limit; inspect retained receipts before rebuilding')
    return None


def plan_ios(values, event, name, ref, sha, inputs):
    values.update(run=False, package=False, stable=False, staging=True, ios=False,
                  ios_dispatch=False, tag='', reason='Internal iOS staging selection')
    requested = str(inputs.get('ios_staging', '')).lower() == 'true'
    if requested:
        if name != 'workflow_dispatch' or not ref.startswith('refs/tags/staging-'):
            raise ValueError('Privileged iOS staging dispatch must use an immutable staging tag, never a branch/PR')
        if inputs.get('staging_sha') and inputs['staging_sha'] != sha:
            raise ValueError('iOS staging_sha must equal the immutable tag source')
        tag = ref[10:]
        manifest, head = qualified_staging_snapshot(tag, sha)
        values.update(sha=sha, tag=tag, currentIntegrationHead=head)
        ready = ready_ios_artifact(sha)
        if ready:
            values.update(reason=f'unchanged iOS inputs: reuse producing run {ready[0]}, artifact {ready[1]}',
                          ios_artifact_id=ready[1])
        else:
            values.update(ios=True, reason='Qualified source selected for internal export only; device coverage untested')
    else:
        if name != 'schedule' or event.get('schedule') != IOS_CRON or ref != 'refs/heads/main':
            raise ValueError('Weekly iOS selection must run from the trusted main schedule')
        head = full_sha(api(f'repos/{repository()}/git/ref/heads/{STAGING}')['object']['sha'])
        values.update(sha=head, currentIntegrationHead=head)
        releases = api(f'repos/{repository()}/releases?per_page=100')
        candidates = [release for release in releases if not release['draft'] and release['prerelease']
                      and STAGING_TAG.fullmatch(release['tag_name'])]
        if not candidates:
            values['reason'] = 'iOS held: no published qualified staging snapshot; desktop publication is independent'
            return
        chosen = max(candidates, key=lambda item: item['published_at'])
        try:
            manifest, head = qualified_staging_snapshot(chosen['tag_name'])
        except ValueError as error:
            values['reason'] = 'iOS held: ' + str(error).replace('\n', ' ')
            return
        values.update(sha=manifest['sourceCommit'], tag=manifest['tag'], currentIntegrationHead=head,
                      ios_dispatch=True,
                      reason=f'Weekly internal iOS request for qualified {manifest["sourceCommit"]}; integration head is {head}')


def ios_inputs(args):
    manifest, head = qualified_staging_snapshot(args.tag, full_sha(args.sha))
    write_json(args.output, manifest)
    emit(dict(sourceCommit=manifest['sourceCommit'], tag=manifest['tag'],
              currentIntegrationHead=head, deviceCoverage='untested',
              testflightPublication='not-requested'))


def validate_ios_build_number(value):
    if not re.fullmatch(r'[1-9][0-9]{0,3}\.(?:0|[1-9][0-9]?)\.(?:0|[1-9][0-9]?)', value):
        raise ValueError('Internal iOS CFBundleVersion requires canonical 4/2/2 digit numeric components')
    return value


def ios_build_number(run_number, attempt):
    number, attempt = int(run_number), int(attempt)
    if number < 1 or not 1 <= attempt <= 99:
        raise ValueError('Internal iOS requires a positive workflow run number and attempt 1..99')
    # One namespace for this existing workflow, monotonic by run then attempt.
    # Never wrap either component and accidentally reuse an App Store build ID.
    return validate_ios_build_number(f'{1000 + number // 100}.{number % 100}.{attempt}')


def plan(args):
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    name = os.environ['GITHUB_EVENT_NAME']
    ref = os.environ['GITHUB_REF']
    sha = full_sha(os.environ['GITHUB_SHA'])
    inputs = event.get('inputs') or {}
    probes = any(str(inputs.get(key, '')).lower() == 'true' for key in
                 ('windows_cache_probe', 'linux_audio_probe', 'android_package_probe', 'macos_skin_probe'))
    stable = name == 'push' and ref.startswith('refs/tags/') and bool(STABLE.fullmatch(ref[10:]))
    values = dict(sha=sha, run=True, package=stable or probes, stable=stable,
                  staging=False, ios=False, ios_dispatch=False, tier='integration',
                  tag=ref[10:] if stable else '', reason='integration CI')
    if str(inputs.get('ios_staging', '')).lower() == 'true' or (name == 'schedule' and event.get('schedule') == IOS_CRON):
        if probes:
            raise ValueError('Internal iOS staging cannot be combined with another validation probe')
        plan_ios(values, event, name, ref, sha, inputs)
        write_json(args.output, values)
        emit(values)
        return
    if name == 'push' and ref.startswith('refs/tags/') and not stable:
        values.update(run=False, package=False, reason='Not an official vX.Y.Z stable tag')
    if name == 'pull_request':
        head = event['pull_request']['head']
        if head['ref'] == STAGING and head['repo']['full_name'] == repository():
            values.update(run=False, reason='integration push covers this source; PR does not duplicate it')
    if name == 'push' and ref == 'refs/heads/main':
        try:
            qualified = qualification_for(sha)
        except (ValueError, subprocess.CalledProcessError):
            qualified = None  # Unavailable/ambiguous evidence means run CI.
        if qualified:
            values.update(run=False, reason='exact source already passed integration CI: ' + str(qualified['id']))
        else:
            before = event.get('before', '')
            if SHA.fullmatch(before) and before != '0' * 40:
                changed = api(f'repos/{repository()}/compare/{before}...{sha}').get('files', [])
                if docs_only([item['filename'] for item in changed]):
                    values.update(run=False, reason='main documentation-only change: no duplicate platform build')
    packaging = name == 'schedule' or (name == 'workflow_dispatch' and bool(inputs.get('staging_sha')))
    if packaging:
        if probes:
            raise ValueError('A staging package request cannot also be a validation probe')
        if ref not in ('refs/heads/main', 'refs/heads/' + STAGING):
            raise ValueError('Staging package control workflow must use trusted main/integration')
        head = api(f'repos/{repository()}/git/ref/heads/{STAGING}')['object']['sha']
        sha = full_sha(inputs.get('staging_sha') or head)
        if sha != head and api(f'repos/{repository()}/compare/{sha}...{head}')['status'] not in ('ahead', 'identical'):
            raise ValueError('Manual staging SHA must belong to the integration branch history')
        runs = workflow_runs()
        green = any(item['head_sha'] == sha and item['head_branch'] == STAGING
                    and item['event'] == 'push' and item['conclusion'] == 'success' for item in runs)
        receipt = inputs.get('integration_run_id')
        if receipt:
            if not receipt.isdigit():
                raise ValueError('Integration receipt must be a GitHub run ID')
            source_run = api(f'repos/{repository()}/actions/runs/{receipt}')
            if source_run['workflow_id'] != api(f'repos/{repository()}/actions/workflows/build.yml')['id']:
                raise ValueError('Integration receipt belongs to another workflow')
            jobs = api(f'repos/{repository()}/actions/runs/{receipt}/jobs?per_page=100')['jobs']
            green = integration_green(source_run, jobs, sha)
        existing = optional_release(snapshot_tag(sha))
        require_unretired_staging(sha, existing)
        package, reason = package_decision(sha, green, existing, runs,
                                          dt.datetime.now(dt.timezone.utc), name == 'workflow_dispatch' and not receipt,
                                          os.environ.get('GITHUB_RUN_ID'))
        nightly = event.get('schedule') == '47 3 * * *'
        values.update(sha=sha, run=package or nightly, package=package, stable=False, staging=True,
                      tag=snapshot_tag(sha), reason=reason, tier='nightly' if nightly else 'integration')
    if str(inputs.get('nightly_probe', '')).lower() == 'true':
        # The nightly tier otherwise runs once a day from its cron, so a defect
        # in it could not be reproduced or checked on demand.
        if probes or packaging or stable or name != 'workflow_dispatch':
            raise ValueError('The nightly tier probe runs alone and publishes nothing')
        values.update(tier='nightly', reason='nightly tier on demand')
    if stable:
        # Fail before admitting any platform/package job, not after signing work.
        with tempfile.TemporaryDirectory(prefix='release-readiness-') as directory:
            reuse_qualification(argparse.Namespace(sha=sha, directory=Path(directory)))
    write_json(args.output, values)
    emit(values)


def dispatch_staging(args):
    sha = full_sha(args.sha)
    existing = optional_release(snapshot_tag(sha))
    require_unretired_staging(sha, existing)
    selected, reason = package_decision(sha, True, existing, workflow_runs(),
                                       dt.datetime.now(dt.timezone.utc), current_run=os.environ.get('GITHUB_RUN_ID'))
    print(reason)
    if selected:
        subprocess.run(['gh', 'workflow', 'run', 'build.yml', '--repo', repository(), '--ref', STAGING,
                        '-f', 'staging_sha=' + sha,
                        '-f', 'integration_run_id=' + os.environ['GITHUB_RUN_ID']], check=True)


def desktop_names(version):
    return {f'DuneCity-{version}-Windows-x64.exe', f'DuneCity-{version}-Windows-x64.zip',
            f'DuneCity-{version}-macOS.dmg', f'DuneCity-{version}-macOS.zip',
            f'DuneCity-{version}-Linux-x86_64.AppImage', f'dunecity_{version}_amd64.deb',
            f'DuneCity-{version}-Linux-x64.rpm', f'DuneCity-{version}-Linux-x64.tar.gz'}


def asset_paths(directory):
    paths = {}
    for path in Path(directory).rglob('*'):
        if not path.is_file() or path.name in (MANIFEST, 'SHA256SUMS'):
            continue
        if path.name in paths:
            raise ValueError('Duplicate publication filename: ' + path.name)
        paths[path.name] = path
    return paths


def verify_updates(paths, version, repo=None, public_key=None):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    public = public_key or base64.b64decode((ROOT / 'cmake/update-public-key.txt').read_text().strip())
    key = Ed25519PublicKey.from_public_bytes(public)
    for platform, filename in [('macos-arm64', f'DuneCity-{version}-macOS.zip'),
                               ('windows-x64', f'DuneCity-{version}-Windows-x64.exe'),
                               ('linux-x86_64', f'DuneCity-{version}-Linux-x86_64.AppImage')]:
        archive = paths[filename]
        rows = paths[f'updates-{platform}.txt'].read_bytes().splitlines(keepends=True)
        if len(rows) != 7 or any(not row.endswith(b'\n') for row in rows):
            raise ValueError('Malformed signed updater feed: ' + platform)
        payload = b''.join(rows[:6])
        key.verify(base64.b64decode(rows[6]), payload)
        expected = ['DuneCityUpdate1', version, platform,
                    f'https://github.com/{repo or repository()}/releases/download/v{version}/{filename}',
                    str(archive.stat().st_size), digest(archive)]
        if payload.decode().splitlines() != expected:
            raise ValueError('Updater feed disagrees with frozen package: ' + platform)
        if not platform.startswith('linux'):
            enclosure = ET.parse(paths[f'appcast-{platform}.xml']).find('channel/item/enclosure')
            if enclosure is None or enclosure.get('url') != expected[3] or enclosure.get('length') != expected[4] or enclosure.get(NS + 'version') != version:
                raise ValueError('Appcast disagrees with frozen package: ' + platform)
            key.verify(base64.b64decode(enclosure.attrib[NS + 'edSignature']), archive.read_bytes())


def release_token(state, sha, checked_out):
    full_sha(sha)
    token = state.get('id') if isinstance(state, dict) else None
    if (not isinstance(state, dict) or state.get('active') is not True
            or state.get('sid') != sha or checked_out != sha or not isinstance(token, str)
            or not token or any(char.isspace() for char in token)):
        raise ValueError('Mini release admission blocked: require an active reservation for the exact checked-out source SHA')
    return token


def reservation_token(args):
    try:
        state = json.loads(run('dunecity-queue', 'reservation', 'status'))
    except (subprocess.CalledProcessError, OSError, ValueError) as error:
        raise SystemExit('Mini release admission blocked: authoritative reservation status is unreadable') from error
    print(release_token(state, args.sha, run('git', 'rev-parse', 'HEAD')))


def qualification_image_version():
    if os.environ.get('RUNNER_ENVIRONMENT') == 'self-hosted':
        # Name the toolchain that actually compiles: the active developer directory and
        # its compiler. The Mini uses the Command Line Tools, which have no xcodebuild.
        return ':'.join(('self-hosted', run('sw_vers', '-buildVersion'), run('xcode-select', '-p'),
                         run('clang', '--version').splitlines()[0]))
    return os.environ.get('ImageVersion')


def exception_policy(sha, runner_image_version=None):
    """Only committed, explicitly scoped decisions can accept a known failure."""
    policy = json.loads(run('git', 'show', full_sha(sha) + ':' + EXCEPTIONS))
    if policy.get('schema') != 1 or not isinstance(policy.get('exceptions'), list):
        raise ValueError('Invalid qualification exception policy')
    decisions = {}
    runner_image_version = runner_image_version or qualification_image_version()
    for item in policy['exceptions']:
        name = item['test']
        if (not name or name in decisions or not item.get('owner') or not item.get('reason')
                or not item.get('trackingUrl', '').startswith('https://')
                or not re.fullmatch(r'[0-9a-f]{64}', item.get('failureSha256', ''))
                or not set(EXCEPTION_INPUTS).issubset(item.get('inputTree', {}))):
            raise ValueError('Incomplete/duplicate known-failure decision')
        if not item.get('runnerImageVersion') or item['runnerImageVersion'] != runner_image_version:
            raise ValueError('Known-failure toolchain image changed: ' + name)
        for path, expected in item['inputTree'].items():
            if path.startswith(('/', '-')) or '..' in Path(path).parts:
                raise ValueError('Invalid exception input path')
            # ls-tree also represents an absent optional input unambiguously.
            actual = run('git', 'ls-tree', sha, '--', path)
            if actual != expected:
                raise ValueError('Known-failure input scope changed: ' + name + ': ' + path)
        decisions[name] = item
    return decisions


def failure_digest(case):
    # Probe logs include random scratch paths/timings. Only explicit failure
    # markers form the signature; retain the full raw XML separately. Tests
    # without stable structured markers cannot receive a carry-forward waiver.
    output = case.findtext('system-out', '') + '\n' + case.findtext('system-err', '')
    markers = re.findall(r'\b(?:[A-Z][A-Z0-9_]*_)?(?:FAIL|ERROR):[^\r\n]+', output)
    if not markers:
        raise ValueError('A known failure requires structured FAIL/ERROR markers')
    evidence = dict(markers=sorted(line.strip() for line in markers),
                    failures=[(child.tag, sorted(child.attrib.items()), ''.join(child.itertext()))
                              for child in case if child.tag in ('failure', 'error')])
    return hashlib.sha256(json.dumps(evidence, sort_keys=True).encode()).hexdigest()



def headless_qualification(directory, decisions=None):
    """Freeze actual coverage; retain FAIL separately from explicit acceptance."""
    decisions = decisions or {}
    plan = json.loads((directory / HEADLESS_PLAN).read_text())
    selected = plan['tests']
    names = [item['name'] for item in selected]
    labels = [set(next((prop['value'] for prop in item['properties'] if prop['name'] == 'LABELS'), []))
              for item in selected]
    if (not names or len(set(names)) != len(names)
            or any(not {'headless', 'nightly'}.issubset(values) or 'gui' in values for values in labels)):
        raise ValueError('Hosted qualification must select nonempty headless nightly coverage')
    cases = list(ET.parse(directory / HEADLESS_RESULTS).getroot().iter('testcase'))
    if (sorted(item.get('name') for item in cases) != sorted(names)
            or any(item.get('status', 'run') not in ('run', 'passed', 'fail') for item in cases)
            or any(child.tag == 'skipped' for item in cases for child in item.iter())):
        raise ValueError('Hosted headless qualification is failed, skipped, missing or incomplete')
    accepted = []
    for case in cases:
        if case.get('status') == 'fail' or any(child.tag in ('failure', 'error') for child in case.iter()):
            decision = decisions.get(case.get('name'))
            if not decision or decision['failureSha256'] != failure_digest(case):
                raise ValueError('Hosted headless qualification is failed, skipped or has an unaccepted failure: ' + case.get('name', ''))
            accepted.append(decision)
    result = dict(status='accepted-known-failures' if accepted else 'success', capability='headless',
                  tier='nightly', tests=sorted(names), planSha256=digest(directory / HEADLESS_PLAN),
                  resultsSha256=digest(directory / HEADLESS_RESULTS))
    if accepted:
        result['acceptedFailures'] = sorted(accepted, key=lambda item: item['test'])
    return result


def qualification_read(*command):
    """Bounded retries for read-only GitHub 5xx failures, never for auth/404."""
    for attempt in range(3):
        try:
            return subprocess.check_output(command, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as error:
            detail = (error.stderr or b'').decode(errors='replace')
            if attempt == 2 or not re.search(r'HTTP (?:500|502|503|504)\b', detail):
                raise
            time.sleep(attempt + 1)


def qualification_api(path):
    return json.loads(qualification_read('gh', 'api', path))


def qualification_producer(receipt, sha):
    """Authenticate the frozen producing attempt, independently of this run."""
    qid, attempt = int(receipt['qualifyingRunId']), int(receipt['qualifyingRunAttempt'])
    if qid <= 0 or attempt <= 0:
        raise ValueError('Invalid qualification producing run/attempt')
    prefix = f'repos/{repository()}'
    producer = qualification_api(f'{prefix}/actions/runs/{qid}/attempts/{attempt}')
    workflow = qualification_api(f'{prefix}/actions/workflows/qualify-release.yml')
    if (producer['id'] != qid or producer['run_attempt'] != attempt
            or producer['workflow_id'] != workflow['id'] or producer['path'] != QUALIFY_WORKFLOW
            or producer['status'] != 'completed' or producer['conclusion'] != 'success'
            or producer['event'] not in ('workflow_run', 'workflow_dispatch')
            or producer['head_branch'] not in ('main', STAGING)
            or producer['head_repository']['full_name'] != repository()):
        raise ValueError('Untrusted or unsuccessful qualification producer')
    # The trusted control workflow and candidate must use the same recipe.
    source_blob = qualification_api(f'{prefix}/contents/{QUALIFY_WORKFLOW}?ref={sha}')['sha']
    control_blob = qualification_api(f'{prefix}/contents/{QUALIFY_WORKFLOW}?ref={full_sha(producer["head_sha"])}')['sha']
    if source_blob != control_blob:
        raise ValueError('Qualification control recipe differs from candidate')
    jobs = qualification_api(f'{prefix}/actions/runs/{qid}/attempts/{attempt}/jobs?per_page=100')['jobs']
    if not {'select-candidate', 'qualify'}.issubset({j['name'] for j in jobs if j['conclusion'] == 'success'}):
        raise ValueError('Qualification producer lacks successful required jobs')
    matches = [j for j in jobs if j['name'] == 'qualify']
    if len(matches) != 1:
        raise ValueError('Ambiguous qualification runner')
    job = matches[0]
    environment = receipt.get('runnerEnvironment', 'github-hosted')
    expected = {'self-hosted', 'macOS', 'ARM64', 'dunecity-builder'} if environment == 'self-hosted' else {'macos-15'}
    if (environment not in ('github-hosted', 'self-hosted') or set(job.get('labels', [])) != expected
            or receipt.get('runnerName') and receipt['runnerName'] != job.get('runner_name')):
        raise ValueError('Qualification runner identity differs from producing job')
    return producer


def qualification_artifact(producer):
    prefix = f'repos/{repository()}'
    listing = qualification_api(f'{prefix}/actions/runs/{producer["id"]}/artifacts?per_page=100')
    if listing['total_count'] > 100:
        raise ValueError('Ambiguous qualification artifact listing')
    name = QUALIFY_ARTIFACT + '-' + str(producer['run_attempt'])
    matches = [item for item in listing['artifacts'] if item['name'] == name]
    if len(matches) != 1 or matches[0]['expired']:
        raise ValueError('Qualification artifact missing, expired or ambiguous')
    artifact = matches[0]
    if artifact['size_in_bytes'] > 16 * 1024 * 1024:
        raise ValueError('Qualification artifact exceeds evidence size bound')
    raw = qualification_read('gh', 'api', f'{prefix}/actions/artifacts/{artifact["id"]}/zip')
    if artifact.get('digest') != 'sha256:' + hashlib.sha256(raw).hexdigest():
        raise ValueError('Qualification artifact digest mismatch')
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries = archive.infolist()
        if (sorted(entry.filename for entry in entries) != sorted(QUALIFY_FILES)
                or any(entry.file_size > 16 * 1024 * 1024 or entry.is_dir() for entry in entries)
                or sum(entry.file_size for entry in entries) > 16 * 1024 * 1024):
            raise ValueError('Unexpected qualification artifact contents')
        return {entry.filename: archive.read(entry) for entry in entries}


def verify_headless_qualification(paths, sha, authenticate=True):
    receipt = json.loads(paths[HEADLESS_RECEIPT].read_text())
    if (receipt['sourceCommit'] != sha or receipt['releaseBuild'] != 'OFF'
            or receipt['platform'] != 'macos-arm64-headless-qualification' or receipt.get('packageBuild') is not False
            or receipt['configuration'].get('DUNECITY_RELEASE_BUILD:BOOL') != 'OFF'
            or receipt['configuration'].get('DUNECITY_BUILD_TESTS:BOOL') != 'ON'):
        raise ValueError('Hosted headless qualification source/configuration mismatch')
    if receipt.get('qualificationWorkflow') != QUALIFY_WORKFLOW:
        # Existing immutable releases used same-run qualification. Do not
        # reinterpret them as reusable cross-run evidence.
        if (int(receipt['qualifyingRunId']) <= 0 or int(receipt['qualifyingRunAttempt']) <= 0
                or (os.environ.get('GITHUB_RUN_ID') and str(receipt['qualifyingRunId']) != os.environ['GITHUB_RUN_ID'])):
            raise ValueError('Hosted headless qualification belongs to a different producing run')
        decisions = {}
    else:
        decisions = exception_policy(sha, receipt.get('runnerImageVersion'))
        if authenticate:
            original = qualification_artifact(qualification_producer(receipt, sha))
            if any(paths[name].read_bytes() != original[name] for name in QUALIFY_FILES):
                raise ValueError('Qualification evidence differs from immutable producer artifact')
    if receipt['qualification'] != headless_qualification(paths[HEADLESS_RECEIPT].parent, decisions):
        raise ValueError('Hosted headless qualification receipt does not match its frozen test results')


def qualification_runs():
    # GitHub run search is bounded at 1000; never mistake a truncated history
    # for proof that qualification does not exist.
    for page in range(1, 11):
        data = qualification_api(f'repos/{repository()}/actions/workflows/qualify-release.yml/runs?status=success&per_page=100&page={page}')
        batch = data['workflow_runs']
        yield from batch
        if len(batch) < 100:
            return
    raise ValueError('Qualification search is truncated; provide the frozen --run-id')


def reuse_qualification(args):
    sha = full_sha(args.sha)
    prefix = f'repos/{repository()}'
    run_id = getattr(args, 'run_id', None)
    runs = [qualification_api(f'{prefix}/actions/runs/{int(run_id)}')] if run_id else qualification_runs()
    # A run's head SHA is the control workflow, not necessarily its candidate.
    for producer in runs:
        if not run_id and producer.get('display_title') != 'Qualify ' + sha:
            continue  # Discovery hint only; the receipt/API still establish trust.
        try:
            contents = qualification_artifact(producer)
            receipt = json.loads(contents[HEADLESS_RECEIPT])
            if receipt['sourceCommit'] != sha:
                continue
            if (str(receipt['qualifyingRunId']) != str(producer['id'])
                    or int(receipt['qualifyingRunAttempt']) != producer['run_attempt']
                    or receipt.get('qualificationWorkflow') != QUALIFY_WORKFLOW):
                raise ValueError('Qualification receipt producer mismatch')
            qualification_producer(receipt, sha)
            with tempfile.TemporaryDirectory(prefix='qualification-') as directory:
                paths = {name: Path(directory) / name for name in QUALIFY_FILES}
                for name, path in paths.items():
                    path.write_bytes(contents[name])
                verify_headless_qualification(paths, sha, authenticate=False)
                args.directory.mkdir(parents=True, exist_ok=True)
                for name, path in paths.items():
                    shutil.copyfile(path, args.directory / name)
            print(f'Qualified {sha}: run {producer["id"]}, attempt {producer["run_attempt"]}; no build or test rerun')
            return
        except (KeyError, ValueError) as error:
            print('Ignoring unusable qualification: ' + str(error), file=sys.stderr)
    raise ValueError('No reusable headless qualification for ' + sha +
                     '; qualify this frozen integration candidate before creating a production tag')


def check_headless(args):
    result = headless_qualification(args.directory, exception_policy(run('git', 'rev-parse', 'HEAD')))
    if args.ctest_status not in (0, 8) or (args.ctest_status == 8 and result['status'] != 'accepted-known-failures'):
        raise ValueError('CTest exit status is not explained by accepted test failures')
    print(json.dumps(result, indent=2))


def select_qualification(args):
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    if os.environ['GITHUB_EVENT_NAME'] == 'workflow_run':
        source = event['workflow_run']
        if (source['head_branch'] != STAGING or source['event'] != 'push'
                or source['conclusion'] != 'success' or source['head_repository']['full_name'] != repository()):
            emit(dict(run=False, sha=''))
            return
        sha = full_sha(source['head_sha'])
        latest = qualification_api(f'repos/{repository()}/git/ref/heads/{STAGING}')['object']['sha']
        if sha != latest:
            emit(dict(run=False, sha=sha))  # Coalesce superseded automatic candidates.
            return
    else:
        if os.environ['GITHUB_REF'] not in ('refs/heads/main', 'refs/heads/' + STAGING):
            raise ValueError('Qualification must be dispatched from trusted main/integration')
        sha = full_sha(event['inputs']['sha'])
    qualification_for(sha, read=qualification_api)  # Verify every required platform/test job, not just a green badge.
    prefix = f'repos/{repository()}/contents/{QUALIFY_WORKFLOW}?ref='
    if qualification_api(prefix + sha)['sha'] != qualification_api(prefix + full_sha(os.environ['GITHUB_SHA']))['sha']:
        raise ValueError('Candidate and control qualification recipes differ; integrate the workflow first')
    # Serialized duplicate notifications consume an existing successful receipt.
    with tempfile.TemporaryDirectory(prefix='qualification-discovery-') as directory:
        try:
            reuse_qualification(argparse.Namespace(sha=sha, directory=Path(directory)))
        except ValueError:
            emit(dict(run=True, sha=sha))
        else:
            emit(dict(run=False, sha=sha))


def record_build(args):
    cache = args.build_root / 'CMakeCache.txt'
    metadata = {}
    if cache.exists():
        for line in cache.read_text(errors='replace').splitlines():
            if re.match(r'^(CMAKE_(C|CXX)_COMPILER|CMAKE_BUILD_TYPE|CMAKE_TOOLCHAIN_FILE|VCPKG_TARGET_TRIPLET|DUNECITY_[A-Z0-9_]+):', line):
                key, value = line.split('=', 1)
                metadata[key] = value
    result = dict(sourceCommit=full_sha(run('git', 'rev-parse', 'HEAD')),
        platform=args.platform, releaseBuild=os.environ.get('DUNECITY_RELEASE_BUILD', 'OFF'),
        runnerImage=os.environ.get('ImageOS'), runnerImageVersion=qualification_image_version(),
        runnerEnvironment=os.environ.get('RUNNER_ENVIRONMENT', 'github-hosted'), runnerName=os.environ.get('RUNNER_NAME'),
        runnerArchitecture=os.environ.get('RUNNER_ARCH'), cmake=run('cmake', '--version'),
        configuration=metadata,
        toolchainFileSha256={key: digest(value) for key, value in metadata.items()
                             if key.startswith(('CMAKE_C_COMPILER:', 'CMAKE_CXX_COMPILER:', 'CMAKE_TOOLCHAIN_FILE:'))
                             and Path(value).is_file()})
    if args.platform == 'macos-arm64-headless-qualification':
        result.update(qualification=headless_qualification(args.build_root, exception_policy(result['sourceCommit'])),
                      qualificationWorkflow=QUALIFY_WORKFLOW,
                      qualificationGateSha256={name: digest(ROOT / name) for name in
                                               ('scripts/release-automation.py', 'scripts/macos-ci-lane.sh', EXCEPTIONS)},
                      packageBuild=os.environ.get('PACKAGE_BUILD', 'false') == 'true',
                      qualifyingRunId=os.environ.get('GITHUB_RUN_ID'),
                      qualifyingRunAttempt=os.environ.get('GITHUB_RUN_ATTEMPT'))
    write_json(args.output, result)


def make_manifest(args):
    sha = full_sha(args.sha)
    paths = asset_paths(args.directory)
    version = re.search(r'project\(DuneCity VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    required = desktop_names(version) | {BROWSER, 'DuneCity-p2p-service.tar.gz'}
    required |= {f'build-inputs-{platform}.json' for platform in
                 ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten')}
    if args.stable:
        if args.tag != 'v' + version or not STABLE.fullmatch(args.tag):
            raise ValueError('Official package version does not match the stable tag')
        required |= {f'updates-{p}.txt' for p in ('macos-arm64', 'windows-x64', 'linux-x86_64')}
        required |= {'appcast-macos-arm64.xml', 'appcast-windows-x64.xml', 'macos-signing.json'}
        required |= {HEADLESS_RECEIPT, HEADLESS_PLAN, HEADLESS_RESULTS}
    if required - paths.keys():
        raise ValueError('Missing required publication assets: ' + ', '.join(sorted(required - paths.keys())))
    for platform in ('windows-x64', 'macos-arm64', 'linux-x86_64', 'emscripten'):
        metadata = json.loads(paths[f'build-inputs-{platform}.json'].read_text())
        if metadata['sourceCommit'] != sha or metadata['releaseBuild'] != ('ON' if args.stable else 'OFF'):
            raise ValueError('Platform package source/official identity mismatch: ' + platform)
        expected_identity = 'ON' if args.stable else 'OFF'
        if metadata['configuration'].get('DUNECITY_RELEASE_BUILD:BOOL') != expected_identity:
            raise ValueError('Platform CMake identity disagrees with publication: ' + platform)
    if args.stable:
        verify_headless_qualification(paths, sha)
        verify_updates(paths, version)
        signing = json.loads(paths['macos-signing.json'].read_text())
        if signing['sourceCommit'] != sha or signing['identity'] != 'Developer ID Application: Stefan van der Wel (34X7AYJZ93)':
            raise ValueError('Missing expected Mac signing identity/source')
        for name in (f'DuneCity-{version}-macOS.zip', f'DuneCity-{version}-macOS.dmg'):
            if signing['sha256'].get(name) != digest(paths[name]):
                raise ValueError('Mac signing receipt does not match package: ' + name)
    elif args.tag != snapshot_tag(sha) or any(name.startswith(('updates-', 'appcast-')) for name in paths):
        raise ValueError('Development publication cannot contain stable feeds or a stable tag')
    with tempfile.TemporaryDirectory(prefix='dunecity-browser-inputs-') as temporary:
        browser = Path(temporary)
        extract_browser(paths[BROWSER], browser)
        build = verify_browser(browser, 'stable' if args.stable else 'staging', sha)
        if build['version'] != version:
            raise ValueError('Browser version differs from desktop packages')
    source_run = api(f'repos/{repository()}/actions/runs/{os.environ["GITHUB_RUN_ID"]}')
    artifacts = api(f'repos/{repository()}/actions/runs/{source_run["id"]}/artifacts?per_page=100')['artifacts']
    manifest = dict(schema=1, tag=args.tag, version=version, sourceCommit=sha,
                    sourceUrl=f'https://github.com/{repository()}/tree/{sha}',
                    releaseBuild='ON' if args.stable else 'OFF', recipe=RECIPE,
                    inputIdentity=hashlib.sha256((sha + '\n' + RECIPE).encode()).hexdigest(),
                    runId=source_run['id'], runAttempt=source_run['run_attempt'], repository=repository(),
                    producerSourceCommit=source_run['head_sha'], producerEvent=source_run['event'],
                    producerHeadBranch=source_run['head_branch'], producerWorkflowPath=source_run['path'],
                    artifacts=[{key: item.get(key) for key in ('id', 'name', 'digest', 'size_in_bytes', 'expired')}
                               for item in artifacts],
                    sha256={name: digest(path) for name, path in sorted(paths.items())})
    if not args.stable:
        qualified = qualification_for(sha)
        manifest.update(qualificationRunId=qualified['id'], qualificationRunAttempt=qualified['run_attempt'],
                        qualificationSourceCommit=qualified['head_sha'])
    write_json(Path(args.directory) / MANIFEST, manifest)
    (Path(args.directory) / 'SHA256SUMS').write_text(''.join(f'{checksum}  {name}\n' for name, checksum in manifest['sha256'].items()))


def verify_manifest(directory, manifest, tag=None, sha=None):
    if tag and manifest['tag'] != tag:
        raise ValueError('Publication tag does not match its frozen manifest')
    if sha and manifest['sourceCommit'] != sha:
        raise ValueError('Publication source does not match the selected commit')
    full_sha(manifest['sourceCommit'])
    paths = asset_paths(directory)
    if set(paths) != set(manifest['sha256']):
        raise ValueError('Frozen publication asset set differs from its manifest')
    for name, checksum in manifest['sha256'].items():
        if Path(name).name != name or name not in paths or digest(paths[name]) != checksum:
            raise ValueError('Frozen publication hash mismatch: ' + name)
    return paths


def publish(args):
    directory = Path(args.directory)
    manifest = json.loads((directory / MANIFEST).read_text())
    paths = verify_manifest(directory, manifest, args.tag, args.sha)
    stable = manifest['releaseBuild'] == 'ON'
    if stable != bool(STABLE.fullmatch(args.tag)):
        raise ValueError('Release identity/channel mismatch')
    if not stable and args.tag != snapshot_tag(args.sha):
        raise ValueError('Development publication requires its immutable source/input tag')
    existing = optional_release(args.tag)
    if existing and (existing['prerelease'] == stable
                     or (existing['draft'] and existing['target_commitish'] != args.sha)
                     or (not existing['draft'] and resolve_tag(args.tag) != args.sha)):
        raise ValueError('Existing publication channel/tag source differs from selected inputs')
    if existing and not existing['draft']:
        # A later website failure may rerun this job. Read back the frozen set;
        # never rewrite immutable inputs or change Latest on that recovery edge.
        with tempfile.TemporaryDirectory(prefix='dunecity-published-inputs-') as temporary:
            frozen = Path(temporary)
            download_release(args.tag, frozen, [MANIFEST])
            original = json.loads((frozen / MANIFEST).read_text())
        if original['sourceCommit'] != args.sha or original['sha256'] != manifest['sha256']:
            raise ValueError('Refusing to change a published release; use a new version')
        assets = {item['name']: item for item in existing['assets']}
        for name, checksum in original['sha256'].items():
            if assets.get(name, {}).get('digest') != 'sha256:' + checksum:
                raise ValueError('Published asset readback failed: ' + name)
        print('Published immutable inputs already match; resuming downstream delivery only')
        if not stable:
            return complete_staging_publication(args.tag, args.sha)
        return
    if not stable:
        ensure_staging_tag(args.tag, args.sha)
    elif resolve_tag(args.tag) != args.sha:
        raise ValueError('Stable tag does not point at the selected source')
    files = [str(path) for path in paths.values()] + [str(directory / MANIFEST), str(directory / 'SHA256SUMS')]
    notes = (f'Frozen source {args.sha}; producing run {manifest["runId"]}. '
             'All desktop platforms complete; mobile channels are independent.\n\n')
    release_notes = ROOT / 'releases/desktop' / f'{manifest["version"]}.md'
    if stable and release_notes.exists():
        notes += release_notes.read_text()
    if existing:
        assets = {item['name']: item for item in existing['assets']}
        if len(assets) != len(existing['assets']) or set(assets) - {Path(item).name for item in files}:
            raise ValueError('Draft contains assets outside the selected immutable package set')
        pending = []
        for filename in files:
            path = Path(filename)
            item = assets.get(path.name)
            if item is None:
                pending.append(filename)
            elif item['size'] != path.stat().st_size or item.get('digest') != 'sha256:' + digest(path):
                raise ValueError('Refusing to rewrite a frozen draft asset: ' + path.name)
        if pending:
            subprocess.run(['gh', 'release', 'upload', args.tag, '--repo', repository(), *pending], check=True)
            remote = optional_release(args.tag)
        else:
            remote = existing
    else:
        subprocess.run(['gh', 'release', 'create', args.tag, '--repo', repository(), '--target', args.sha,
                        '--draft', '--title', ('Dune City ' if stable else 'Staging ') + args.tag,
                        '--notes', notes,
                        *([] if stable else ['--prerelease', '--latest=false']), *files], check=True)
        remote = optional_release(args.tag)
    if resolve_tag(args.tag) != args.sha:
        raise ValueError('Uploaded draft tag does not point at the selected source')
    if (not remote or not remote['draft'] or remote['prerelease'] == stable
            or remote['target_commitish'] != args.sha
            or (existing and remote['id'] != existing['id'])):
        raise ValueError('Draft publication identity/channel/source readback failed')
    assets = {item['name']: item for item in remote['assets']}
    if len(assets) != len(remote['assets']) or set(assets) != {Path(item).name for item in files}:
        raise ValueError('Draft contains assets outside the selected immutable package set')
    for path in [Path(item) for item in files]:
        item = assets.get(path.name)
        if not item or item['size'] != path.stat().st_size or item.get('digest') != 'sha256:' + digest(path):
            raise ValueError('Draft upload readback failed: ' + path.name)
    subprocess.run(['gh', 'api', '--method', 'PATCH', f'repos/{repository()}/releases/{remote["id"]}',
                    '-F', 'draft=false', '-f', 'make_latest=' + ('true' if stable else 'false')], check=True)
    release = optional_release(args.tag)
    if not release or release['id'] != remote['id'] or release['draft'] or release['prerelease'] == stable:
        raise ValueError('Published release channel readback failed')
    if stable and api(f'repos/{repository()}/releases/latest')['tag_name'] != args.tag:
        raise ValueError('GitHub Latest did not advance to the verified release')
    print(f'Published {args.tag} from {args.sha}, producing run {manifest["runId"]}')
    if not stable:
        return complete_staging_publication(args.tag, args.sha)


def staging_lifecycle_owner():
    """The existing FIFO dev-release job is the sole pointer/deletion writer."""
    if (os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('GITHUB_JOB') != 'dev-release'
            or os.environ.get('DUNECITY_STAGING_LIFECYCLE') != 'true'):
        raise ValueError('Mutation requires the admitted Game staging-lifecycle job mutex')
    run_id, attempt = os.environ.get('GITHUB_RUN_ID', ''), os.environ.get('GITHUB_RUN_ATTEMPT', '')
    if not run_id.isdigit() or not attempt.isdigit() or int(run_id) <= 0 or int(attempt) <= 0:
        raise ValueError('Cannot establish the current lifecycle run/attempt')
    owner = api(f'repos/{repository()}/actions/runs/{run_id}')
    if (owner['id'] != int(run_id) or owner['run_attempt'] != int(attempt)
            or owner['path'] != BUILD_WORKFLOW or owner['status'] != 'in_progress'
            or owner['conclusion'] is not None or owner['repository']['full_name'] != repository()):
        raise ValueError('Cannot establish the current lifecycle owner')
    return owner


def staging_pointer_tag(body, allow_legacy=False):
    body = '' if body is None else body
    if not isinstance(body, str):
        raise ValueError('latest-dev body is not text')
    markers = STAGING_POINTER.findall(body)
    if allow_legacy and not markers and 'dunecity-staging-tag' not in body:
        return None
    if len(markers) != 1 or body.count('dunecity-staging-tag') != 1:
        raise ValueError('latest-dev has no unambiguous machine-readable known-good staging tag')
    return markers[0]


def staging_pointer_body(body, tag):
    """Replace only our bounded block/marker, preserving all historical notes."""
    previous = staging_pointer_tag(body, allow_legacy=True)
    block = (f'{POINTER_BEGIN}\n## Current immutable staging snapshot\n\n'
             f'[{tag}](https://github.com/{repository()}/releases/tag/{tag})\n\n'
             'This link identifies verified immutable artifact publication, not full UI or production qualification. '
             'Download current runtime packages from that snapshot. Attachments and source archives on this '
             'legacy latest-dev release remain historical and do not identify the current runtime.\n\n'
             f'<!-- dunecity-staging-tag: {tag} -->\n{POINTER_END}')
    if POINTER_BEGIN in body or POINTER_END in body:
        if body.count(POINTER_BEGIN) != 1 or body.count(POINTER_END) != 1:
            raise ValueError('latest-dev managed pointer block is ambiguous')
        start, end = body.index(POINTER_BEGIN), body.index(POINTER_END)
        marker = STAGING_POINTER.search(body)
        if not previous or not marker or not start < marker.start() < end:
            raise ValueError('latest-dev managed pointer block is malformed')
        return body[:start] + block + body[end + len(POINTER_END):]
    if previous:
        marker = STAGING_POINTER.search(body)
        return body[:marker.start()] + block + body[marker.end():]
    return block + ('\n\n' + body if body else '')


def advance_staging_pointer(tag, sha):
    """Reconcile only latest-dev's body after independent immutable verification."""
    report = dict(status='held', tag=tag, previousTag=None, reason='')
    errors = (KeyError, TypeError, ValueError, RuntimeError, OSError, subprocess.CalledProcessError)
    try:
        owner = staging_lifecycle_owner()
        qualified_staging_snapshot(tag, full_sha(sha), publication=owner)
        pointer = optional_release('latest-dev')
        if pointer and (pointer['draft'] or not pointer['prerelease']):
            raise ValueError('latest-dev is not a published prerelease pointer')
        body = pointer.get('body') if pointer else ''
        body = '' if body is None else body
        previous = staging_pointer_tag(body, allow_legacy=True)
        report['previousTag'] = previous
        if previous:
            target = optional_release(previous)
            if not target or target['draft'] or not target['prerelease']:
                raise ValueError('Previous latest-dev target is not a published staging snapshot')
            previous_sha = resolve_tag(previous)
            if previous_sha != STAGING_TAG.fullmatch(previous)[1]:
                raise ValueError('Previous latest-dev tag does not identify its advertised source')
            if previous != tag:
                if previous_sha == sha:
                    raise ValueError('Previous latest-dev source has different immutable inputs; ordering is uncertain')
                ancestry = api(f'repos/{repository()}/compare/{previous_sha}...{sha}')['status']
                if ancestry != 'ahead':
                    raise ValueError('Refusing latest-dev pointer regression; source ancestry is ' + str(ancestry))
        updated = staging_pointer_body(body, tag)
        if pointer and updated == body:
            report.update(status='unchanged', reason='latest-dev already identifies the verified immutable artifact snapshot')
        else:
            if pointer:
                subprocess.run(['gh', 'api', '--method', 'PATCH',
                                f'repos/{repository()}/releases/{pointer["id"]}',
                                '-f', 'body=' + updated], check=True, capture_output=True, text=True)
            else:
                # gh creates a missing tag but never moves an existing legacy ref.
                subprocess.run(['gh', 'release', 'create', 'latest-dev', '--repo', repository(),
                                '--target', sha, '--prerelease', '--latest=false',
                                '--title', 'Development staging pointer', '--notes', updated],
                               check=True, capture_output=True, text=True)
            observed = optional_release('latest-dev')
            if (not observed or observed['draft'] or not observed['prerelease']
                    or observed.get('body') != updated or staging_pointer_tag(observed['body']) != tag
                    or (pointer and any(observed[key] != pointer[key] for key in
                                        ('id', 'tag_name', 'target_commitish', 'assets')))
                    or (not pointer and observed['assets'])):
                raise ValueError('latest-dev pointer body/channel/history readback failed')
            report.update(status='advanced', reason='Verified immutable artifact snapshot linked; historical release inputs preserved')
    except errors as error:
        detail = (getattr(error, 'stderr', None) or str(error)).strip()
        report['reason'] = 'pointer held: ' + detail
    if os.environ.get('RUNNER_TEMP'):
        write_json(Path(os.environ['RUNNER_TEMP']) / 'staging-pointer.json', report)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write('\n### Staging pointer (artifact publication, not UI/production qualification)\n```json\n' +
                         json.dumps(report, indent=2) + '\n```\n')
    print(json.dumps(report, indent=2))
    return report


def complete_staging_publication(tag, sha):
    pointer = advance_staging_pointer(tag, sha)
    retention = prune_staging(argparse.Namespace(
        current=tag, dry_run=False, pointer_hold=pointer['reason'] if pointer['status'] == 'held' else ''))
    return dict(pointer=pointer, retention=retention)


def active_runs(repo):
    """Read all active consumers; an incomplete API view cannot authorize deletion."""
    seen = set()
    for status in ('queued', 'in_progress', 'waiting', 'pending', 'requested'):
        for page in range(1, 11):
            data = api(f'repos/{repo}/actions/runs?status={status}&per_page=100&page={page}')
            if not isinstance(data['total_count'], int) or data['total_count'] > 1000:
                raise ValueError('Active consumer history is truncated: ' + repo)
            runs = data['workflow_runs']
            for source in runs:
                if source.get('status') not in ('queued', 'in_progress', 'waiting', 'pending', 'requested'):
                    raise ValueError('Unknown active consumer status: ' + repo)
                if source['id'] not in seen:
                    seen.add(source['id'])
                    yield source
            if len(runs) < 100:
                break
        else:
            raise ValueError('Active consumer pagination is incomplete: ' + repo)


def staging_request_pin(ref):
    website = 'VR48/dunelegacy.com'
    data = api(f'repos/{website}/contents/deploy/staging-browser.json?ref={ref}')
    if data['type'] != 'file' or data['encoding'] != 'base64':
        raise ValueError('Website consumer request is not a readable immutable file')
    request = json.loads(base64.b64decode(data['content']))
    tag = request['releaseTag']
    match = STAGING_TAG.fullmatch(tag)
    if (request['schema'] != 1 or request['repository'] != repository() or not match
            or match[1] != full_sha(request['sourceCommit'])):
        raise ValueError('Website consumer request has ambiguous staging identity')
    full_sha(request['websiteCodeCommit'])
    for field in ('publicationManifestSha256', 'browserArchiveSha256', 'serviceArchiveSha256'):
        if not re.fullmatch(r'[0-9a-f]{64}', request[field]):
            raise ValueError('Website consumer request has missing artifact hashes')
    return tag


def staging_consumer_pins():
    # Publish/prune and Game browser-request admission share a Game job mutex.
    # Website jobs are in another repository: their own concurrency is NOT that lock.
    pins = {staging_request_pin('main')}
    for source in active_runs(repository()):
        branch = source.get('head_branch') or ''
        if not branch:
            raise ValueError(f'Game consumer {source["id"]} has an unresolved source ref')
        if STAGING_TAG.fullmatch(branch):
            pins.add(branch)
        if source['path'] == '.github/workflows/web.yml':
            # Dispatch inputs are not exposed by the run API. A pending request
            # has not entered the mutex yet: retain rather than guess its tag.
            raise ValueError(f'Game browser consumer {source["id"]} has unresolved dispatch inputs')
    for source in active_runs('VR48/dunelegacy.com'):
        if source['path'] == '.github/workflows/deploy-metaserver-droplet.yml':
            pins.add(staging_request_pin(full_sha(source['head_sha'])))
    return pins


def staging_retention_plan(releases, current, known_good, pins=(), uncertain=None):
    published = [item for item in releases if not item['draft'] and item['prerelease']
                 and STAGING_TAG.fullmatch(item['tag_name'])]
    held, dated = {}, []
    for item in published:
        try:
            dated.append((iso_time(item['published_at']), item))
        except (AttributeError, TypeError, ValueError):
            held[item['tag_name']] = 'unknown publication date'
    newest = {item['tag_name'] for _, item in sorted(dated, key=lambda pair: pair[0], reverse=True)[:7]}
    protected = set(pins) | newest | {tag for tag in (current, known_good) if tag}
    eligible = []
    for item in published:
        tag, body = item['tag_name'], item.get('body') or ''
        if tag in held:
            continue
        if tag in protected:
            held[tag] = 'current/newest-seven/known-good/active-consumer pin'
        elif uncertain:
            held[tag] = 'retention held: ' + uncertain
        elif not isinstance(body, str):
            held[tag] = 'unknown investigation hold body'
        elif 'dunecity-retain' in body:
            reasons = re.findall(r'<!--\s*dunecity-retain:\s*([^<>]+?)\s*-->', body)
            held[tag] = ('investigation: ' + '; '.join(reason.strip() for reason in reasons)
                         if reasons and all(reason.strip() for reason in reasons)
                         else 'unknown investigation hold syntax')
        else:
            eligible.append(item)
    return eligible, held


def prune_staging(args):
    """Keep newest seven plus consumers/known-good; never rewrite immutable staging bodies.

    latest-dev body: <!-- dunecity-staging-tag: staging-FULLSHA-INPUT12 -->
    An individual staging body may hold: <!-- dunecity-retain: investigation reason -->
    Missing/ambiguous authority retains inputs. Only the admitted FIFO CI job may delete.
    """
    report = dict(deleted=[], eligible=[], held={}, reason='')
    errors = (KeyError, TypeError, ValueError, RuntimeError, OSError, subprocess.CalledProcessError)
    try:
        if getattr(args, 'pointer_hold', ''):
            raise ValueError('latest-dev reconciliation did not complete: ' + args.pointer_hold)
        releases = []
        for page in range(1, 1001):
            batch = api(f'repos/{repository()}/releases?per_page=100&page={page}')
            if not isinstance(batch, list):
                raise ValueError('Release listing is incomplete')
            releases.extend(batch)
            if len(batch) < 100:
                break
        else:
            raise ValueError('Release pagination is incomplete')
        pointer = optional_release('latest-dev')
        if not pointer or pointer['draft'] or not pointer['prerelease']:
            raise ValueError('latest-dev is not a published prerelease pointer')
        body = pointer.get('body') if pointer else None
        known_good = staging_pointer_tag(body)
        if not any(item['tag_name'] == known_good and not item['draft'] and item['prerelease'] for item in releases):
            raise ValueError('latest-dev known-good target is not a published staging snapshot')
        pins = staging_consumer_pins()
        if not args.dry_run:
            staging_lifecycle_owner()
        eligible, report['held'] = staging_retention_plan(releases, args.current, known_good, pins)
        report['eligible'] = [item['tag_name'] for item in eligible]
        for item in eligible:
            tag = item['tag_name']
            try:
                # Do not classify malformed/partially published old inputs as
                # safe garbage merely because their names resemble snapshots.
                with tempfile.TemporaryDirectory(prefix='dunecity-retention-') as temporary:
                    folder = Path(temporary)
                    download_release(tag, folder, [MANIFEST])
                    manifest = json.loads((folder / MANIFEST).read_text())
                if (manifest['tag'] != tag or manifest['releaseBuild'] != 'OFF'
                        or tag != snapshot_tag(manifest['sourceCommit'], manifest['recipe'])
                        or resolve_tag(tag) != manifest['sourceCommit']):
                    raise ValueError('Old snapshot publication identity is ambiguous')
                validate_staging_receipts(manifest)
                if not args.dry_run:
                    subprocess.run(['gh', 'release', 'delete', tag, '--repo', repository(), '--yes'], check=True)
                    report['deleted'].append(tag)
            except errors as error:
                report['held'][tag] = 'retention held: ' + str(error)
                if tag in report['eligible']:
                    report['eligible'].remove(tag)
        report['reason'] = 'dry-run; no deletions' if args.dry_run else 'bounded staging retention completed'
    except errors as error:
        report['reason'] = 'retention held: ' + str(error)
    if os.environ.get('RUNNER_TEMP'):
        write_json(Path(os.environ['RUNNER_TEMP']) / 'staging-retention.json', report)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as stream:
            stream.write('\n### Staging retention (not package qualification)\n```json\n' +
                         json.dumps(report, indent=2) + '\n```\n')
    print(json.dumps(report, indent=2))
    return report


def download_release(tag, directory, names=None):
    release = optional_release(tag)
    if not release or release['draft']:
        if STAGING_TAG.fullmatch(tag):
            raise ValueError('Staging snapshot retired or unavailable: ' + tag + '; no implicit rebuild')
        raise ValueError('Required immutable release is not published: ' + tag)
    assets = {item['name']: item for item in release['assets']}
    directory.mkdir(parents=True, exist_ok=True)
    for name in names or assets:
        if Path(name).name != name or name not in assets:
            raise ValueError('Missing/unsafe published artifact: ' + name)
        path = directory / name
        item = assets[name]
        if not path.exists():
            subprocess.run(['gh', 'release', 'download', tag, '--repo', repository(), '--pattern', name,
                            '--dir', str(directory)], check=True)
        if path.stat().st_size != item['size'] or item.get('digest') != 'sha256:' + digest(path):
            raise ValueError('Published artifact hash mismatch: ' + name)
    return release


def extract_browser(archive, destination):
    destination.mkdir(mode=0o755, parents=True, exist_ok=True)
    root = destination.resolve()
    with tarfile.open(archive) as source:
        members = source.getmembers()
        seen = set()
        for item in members:
            relative = Path(item.name)
            target = destination / relative
            if (relative.is_absolute() or '..' in relative.parts or relative in seen
                    or item.issym() or item.islnk() or not (item.isfile() or item.isdir())
                    or (relative == Path('.') and not item.isdir())
                    or any((destination / parent).is_symlink() for parent in (relative, *relative.parents))
                    or not target.resolve().is_relative_to(root)):
                raise ValueError('Unsafe browser artifact member: ' + item.name)
            seen.add(relative)
        # Apply no archive ownership, timestamps or permission bits. Create only
        # ordinary directories/files and stream payloads on the Python 3.9 baseline.
        for item in members:
            relative = Path(item.name)
            directory = relative if item.isdir() else relative.parent
            parent = destination
            for component in directory.parts:
                parent /= component
                parent.mkdir(mode=0o755, exist_ok=True)
            if item.isfile():
                target = destination / relative
                with source.extractfile(item) as stream, target.open('xb') as output:
                    target.chmod(0o644)
                    shutil.copyfileobj(stream, output, length=1 << 20)


def verify_browser(root, channel, sha):
    build = json.loads((root / 'build.json').read_text())
    if build['sourceCommit'] != sha or build['channel'] != channel:
        raise ValueError('Browser channel/source differs from frozen release')
    expected_identity = 'ON' if channel == 'stable' else 'OFF'
    if build['releaseBuild'] != expected_identity:
        raise ValueError('Browser official identity does not match its channel')
    required = {'index.html', 'shell.js', 'shell.css', 'p2p-direct.js',
                'dunecity.js', 'dunecity.wasm', 'dunecity.data'}
    if not required.issubset(build['artifacts']) or set(build['artifacts']) != set(build['sha256']):
        raise ValueError('Browser package has an incomplete runtime artifact set')
    if not (root / '.htaccess').is_file():
        raise ValueError('Browser package is missing its security policy')
    for name, checksum in build['sha256'].items():
        if Path(name).name != name:
            raise ValueError('Unsafe browser artifact filename')
        parts = build.get('split', {}).get(name, [name])
        h = hashlib.sha256()
        for part in parts:
            if Path(part).name != part:
                raise ValueError('Unsafe split browser artifact filename')
            with (root / part).open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    h.update(block)
        if h.hexdigest() != checksum:
            raise ValueError('Browser artifact hash mismatch: ' + name)
    return build


def select_publication(args):
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    tag = args.tag
    if os.environ['GITHUB_EVENT_NAME'] == 'workflow_run':
        source_run = event['workflow_run']
        if (source_run['event'] not in ('push', 'schedule', 'workflow_dispatch')
                or source_run['head_repository']['full_name'] != repository()
                or source_run['path'] != '.github/workflows/build.yml'):
            raise ValueError('Publication source is not a trusted producing workflow')
        artifacts = api(f'repos/{repository()}/actions/runs/{source_run["id"]}/artifacts?per_page=100')['artifacts']
        receipts = [item for item in artifacts if item['name'].startswith('DuneCity-Publication-inputs')
                    and not item['expired']]
        if not receipts:
            emit(dict(eligible=False, reason='No publication receipt: build/package gates did not complete'))
            return
        receipt = max(receipts, key=lambda item: item['id'])
        subprocess.run(['gh', 'run', 'download', str(source_run['id']), '--repo', repository(),
                        '--name', receipt['name'], '--dir', str(args.directory)], check=True)
        manifest = json.loads((args.directory / MANIFEST).read_text())
        if str(manifest['runId']) != str(source_run['id']):
            raise ValueError('Publication receipt belongs to a different producing run')
        tag = manifest['tag']
        if STABLE.fullmatch(tag) and (source_run['head_branch'] != tag or source_run['head_sha'] != manifest['sourceCommit']):
            raise ValueError('Stable receipt does not match its tag run')
    if not tag or not (STABLE.fullmatch(tag) or re.fullmatch(r'staging-[0-9a-f]{40}-[0-9a-f]{12}', tag)):
        raise ValueError('An exact published stable/staging tag is required')
    if args.stable_only and not STABLE.fullmatch(tag):
        emit(dict(eligible=False, reason='SourceForge is stable-only'))
        return
    release = optional_release(tag)
    if args.tag and STAGING_TAG.fullmatch(tag) and not release:
        raise ValueError('Requested staging snapshot is retired or unavailable; browser recovery never rebuilds it')
    eligible = bool(release and not release['draft'])
    emit(dict(eligible=eligible, tag=tag, reason='Published immutable inputs selected' if eligible else 'Release remains unpublished; no downstream deploy'))


def browser_inputs(args):
    tag = args.tag
    channel = 'stable' if STABLE.fullmatch(tag) else 'staging'
    if channel == 'staging' and not re.fullmatch(r'staging-[0-9a-f]{40}-[0-9a-f]{12}', tag):
        raise ValueError('Expected a stable version or immutable staging snapshot')
    release = download_release(tag, args.directory, [MANIFEST, BROWSER])
    if release['prerelease'] != (channel == 'staging'):
        raise ValueError('Browser publication channel mismatch')
    manifest = json.loads((args.directory / MANIFEST).read_text())
    if manifest['tag'] != tag or manifest['sha256'][BROWSER] != digest(args.directory / BROWSER):
        raise ValueError('Frozen browser publication does not match its manifest')
    if channel == 'staging' and snapshot_tag(manifest['sourceCommit'], manifest['recipe']) != tag:
        raise ValueError('Staging input identity mismatch')
    extract_browser(args.directory / BROWSER, args.output)
    build = verify_browser(args.output, channel, manifest['sourceCommit'])
    if channel == 'stable':
        service = 'DuneCity-p2p-service.tar.gz'
        download_release(tag, args.directory, [service])
        if manifest['sha256'][service] != digest(args.directory / service):
            raise ValueError('Frozen signaling service hash mismatch')
        extract_browser(args.directory / service, args.service_output)
    emit(dict(tag=tag, channel=channel, sha=build['sourceCommit'], version=build['version'], run_id=manifest['runId']))


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'Cache-Control': 'no-cache'}), timeout=60) as response:
        return json.load(response)


def verify_live_browser(url, version, sha):
    build = get_json(url.rstrip('/') + '/build.json')
    if build['version'] != version or build['sourceCommit'] != sha:
        raise ValueError(f'Live browser mismatch: desired {version}/{sha}, observed {build["version"]}/{build["sourceCommit"]}')
    for name, checksum in build['sha256'].items():
        if Path(name).name != name:
            raise ValueError('Unsafe live manifest artifact name')
        h = hashlib.sha256()
        with urllib.request.urlopen(url.rstrip('/') + '/' + urllib.parse.quote(name), timeout=180) as response:
            for chunk in iter(lambda: response.read(1024 * 1024), b''):
                h.update(chunk)
        if h.hexdigest() != checksum:
            raise ValueError('Live browser asset hash mismatch: ' + name)
    print(f'Observed browser {version}/{sha} at {url}; all advertised artifact hashes match')


def verify(args):
    if not STABLE.fullmatch(args.tag):
        raise ValueError('Production verification requires an exact stable tag')
    directory = args.directory
    release = download_release(args.tag, directory)
    if release['prerelease']:
        raise ValueError('Expected official stable release')
    version = args.tag[1:]
    sha = resolve_tag(args.tag)
    paths = asset_paths(directory)
    if desktop_names(version) - paths.keys():
        raise ValueError('Published desktop platform set is incomplete')
    embedded_key = api(f'repos/{repository()}/contents/cmake/update-public-key.txt?ref={args.tag}')
    public_key = base64.b64decode(base64.b64decode(embedded_key['content']).strip())
    verify_updates(paths, version, public_key=public_key)
    if (directory / MANIFEST).exists():
        verify_manifest(directory, json.loads((directory / MANIFEST).read_text()), args.tag, sha)
    if sys.platform == 'darwin':
        with tempfile.TemporaryDirectory(prefix='dunecity-signature-') as temporary:
            subprocess.run(['ditto', '-x', '-k', str(paths[f'DuneCity-{version}-macOS.zip']), temporary], check=True)
            app = Path(temporary) / 'dunecity.app'
            subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
            identity = subprocess.run(['codesign', '-dv', '--verbose=4', str(app)], capture_output=True, text=True, check=True).stderr
            if 'Authority=Developer ID Application: Stefan van der Wel (34X7AYJZ93)' not in identity or 'TeamIdentifier=34X7AYJZ93' not in identity:
                raise ValueError('Published Mac package has the wrong signing identity')
            subprocess.run(['xcrun', 'stapler', 'validate', str(app)], check=True)
    print(f'Observed GitHub {args.tag}/{sha}; desktop package hashes and updater signatures verified')
    if args.live:
        latest = api(f'repos/{repository()}/releases/latest')['tag_name']
        if latest != args.tag:
            raise ValueError('GitHub Latest differs from requested production version: ' + latest)
        verify_live_browser('https://dunelegacy.com/play', version, sha)
        defaults = get_json('https://sourceforge.net/projects/dunelegacy/best_release.json').get('platform_releases', {})
        for platform, name in [('windows', f'DuneCity-{version}-Windows-x64.exe'), ('mac', f'DuneCity-{version}-macOS.dmg'), ('linux', f'DuneCity-{version}-Linux-x86_64.AppImage')]:
            if defaults.get(platform, {}).get('filename') != f'/dunecity/{version}/{name}':
                raise ValueError('Observed SourceForge default differs: ' + platform)
        for name in sorted(desktop_names(version) - {f'DuneCity-{version}-macOS.zip'}):
            h = hashlib.sha256()
            with urllib.request.urlopen(f'https://downloads.sourceforge.net/project/dunelegacy/dunecity/{version}/{name}', timeout=180) as response:
                for chunk in iter(lambda: response.read(1024 * 1024), b''):
                    h.update(chunk)
            if h.hexdigest() != digest(paths[name]):
                raise ValueError('Observed SourceForge download hash differs: ' + name)
        for page in ('index.html', 'dune-city.html'):
            with urllib.request.urlopen('https://dunelegacy.com/' + page, timeout=60) as response:
                html = response.read().decode()
            if f'releases/download/{args.tag}/DuneCity-{version}-Windows-x64.exe' not in html or f'New in {version}' not in html:
                raise ValueError('Live website version/copy is not current: ' + page)
        print('Observed SourceForge downloads/defaults and website links/What\'s New match production')
        print('Browser rendering/menu still requires an actual browser smoke; network hashes do not prove rendering.')


def resume(args):
    if args.channel == 'build':
        if not args.run_id or not args.run_id.isdigit():
            raise ValueError('Build recovery requires the exact producing run ID')
        source = api(f'repos/{repository()}/actions/runs/{args.run_id}')
        if (source['path'] != '.github/workflows/build.yml'
                or source['event'] not in ('push', 'schedule', 'workflow_dispatch')
                or not (STABLE.fullmatch(source['head_branch']) or source['display_title'].startswith('Staging packages')
                        or (source['display_title'].startswith('iOS internal staging ') and STAGING_TAG.fullmatch(source['head_branch'])))):
            raise ValueError('Recovery run is not a trusted stable/staging package producer')
        subprocess.run(['gh', 'run', 'rerun', args.run_id, '--failed', '--repo', repository()], check=True)
        return
    if args.channel == 'deploy':
        if not args.run_id or not args.run_id.isdigit():
            raise ValueError('Website deploy retry requires its frozen producing run ID')
        source = api(f'repos/VR48/dunelegacy.com/actions/runs/{args.run_id}')
        if (source['path'] != '.github/workflows/deploy-metaserver-droplet.yml'
                or source['event'] not in ('push', 'workflow_dispatch')):
            raise ValueError('Recovery run is not the frozen website deployment workflow')
        artifacts = api(f'repos/VR48/dunelegacy.com/actions/runs/{args.run_id}/artifacts?per_page=100')['artifacts']
        if not any(item['name'].startswith('Website-deployment-inputs-') and not item['expired'] for item in artifacts):
            raise ValueError('Deployment has no retained frozen input receipt; select an explicit website source_sha instead')
        subprocess.run(['gh', 'run', 'rerun', args.run_id, '--failed', '--repo', 'VR48/dunelegacy.com'], check=True)
        return
    stable = bool(args.tag and STABLE.fullmatch(args.tag))
    staging = bool(args.tag and re.fullmatch(r'staging-[0-9a-f]{40}-[0-9a-f]{12}', args.tag))
    if not stable and not (args.channel == 'browser' and staging):
        raise ValueError('Recovery requires an exact published stable tag or browser staging snapshot')
    release = optional_release(args.tag)
    if not release or release['draft'] or release['prerelease'] != staging:
        raise ValueError('Recovery requires an already-published release in the selected channel')
    names = {item['name'] for item in release['assets']}
    if args.channel == 'browser' and BROWSER not in names:
        if not stable or MANIFEST in names:
            raise ValueError('Published browser archive is missing; refusing to substitute another input')
        # Pre-archive releases (including 865) already have a committed website
        # package. That real immutable tree is the recovery artifact, not a build
        # from an arbitrary current game source or a replacement stable asset.
        website = 'VR48/dunelegacy.com'
        website_sha = full_sha(api(f'repos/{website}/git/ref/heads/main')['object']['sha'])
        content = api(f'repos/{website}/contents/website/play/build.json?ref={website_sha}')
        build = json.loads(base64.b64decode(content['content']))
        if build['version'] != args.tag[1:] or build['sourceCommit'] != resolve_tag(args.tag):
            raise ValueError('Committed website does not contain this exact legacy release; explicit frozen website source is required')
        subprocess.run(['gh', 'workflow', 'run', 'deploy-metaserver-droplet.yml', '--repo', website,
                        '--ref', 'main', '-f', 'source_sha=' + website_sha, '-f', 'channel=site'], check=True)
        print(f'Resuming verified legacy browser source {build["sourceCommit"]} from frozen website {website_sha}; no game build or release asset change')
        return
    subprocess.run(['gh', 'workflow', 'run', 'web.yml' if args.channel == 'browser' else 'sourceforge.yml',
                    '--repo', repository(), '--ref', 'main', '-f', 'tag=' + args.tag], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('select-qualification'); p.set_defaults(func=select_qualification)
    p = commands.add_parser('reuse-qualification'); p.add_argument('--sha', required=True); p.add_argument('--directory', type=Path, required=True); p.add_argument('--run-id', type=int); p.set_defaults(func=reuse_qualification)
    p = commands.add_parser('check-headless'); p.add_argument('--directory', type=Path, required=True); p.add_argument('--ctest-status', type=int, default=0); p.set_defaults(func=check_headless)
    p = commands.add_parser('plan'); p.add_argument('--output', type=Path, default=Path('selection.json')); p.set_defaults(func=plan)
    p = commands.add_parser('dispatch-staging'); p.add_argument('--sha', required=True); p.set_defaults(func=dispatch_staging)
    p = commands.add_parser('manifest'); p.add_argument('--directory', type=Path, required=True); p.add_argument('--sha', required=True); p.add_argument('--tag', required=True); p.add_argument('--stable', action='store_true'); p.set_defaults(func=make_manifest)
    p = commands.add_parser('publish'); p.add_argument('--directory', type=Path, required=True); p.add_argument('--sha', required=True); p.add_argument('--tag', required=True); p.set_defaults(func=publish)
    p = commands.add_parser('prune-staging'); p.add_argument('--current'); p.add_argument('--dry-run', action='store_true'); p.set_defaults(func=prune_staging)
    p = commands.add_parser('browser-inputs'); p.add_argument('--tag', required=True); p.add_argument('--directory', type=Path, required=True); p.add_argument('--output', type=Path, required=True); p.add_argument('--service-output', type=Path, required=True); p.set_defaults(func=browser_inputs)
    p = commands.add_parser('select-publication'); p.add_argument('--tag', default=''); p.add_argument('--directory', type=Path, default=Path('upstream-publication')); p.add_argument('--stable-only', action='store_true'); p.set_defaults(func=select_publication)
    p = commands.add_parser('record-build'); p.add_argument('--platform', required=True); p.add_argument('--build-root', type=Path, required=True); p.add_argument('--output', type=Path, required=True); p.set_defaults(func=record_build)
    p = commands.add_parser('reservation-token'); p.add_argument('--sha', required=True); p.set_defaults(func=reservation_token)
    p = commands.add_parser('ios-build-number'); p.add_argument('--run-number', required=True); p.add_argument('--attempt', required=True); p.set_defaults(func=lambda a: print(ios_build_number(a.run_number, a.attempt)))
    p = commands.add_parser('ios-inputs'); p.add_argument('--tag', required=True); p.add_argument('--sha', required=True); p.add_argument('--output', type=Path, required=True); p.set_defaults(func=ios_inputs)
    p = commands.add_parser('validate-ios-build-number'); p.add_argument('build_number', type=validate_ios_build_number); p.set_defaults(func=lambda a: print(a.build_number))
    p = commands.add_parser('verify'); p.add_argument('tag'); p.add_argument('--directory', type=Path); p.add_argument('--live', action='store_true'); p.set_defaults(func=verify)
    p = commands.add_parser('verify-browser'); p.add_argument('--url', required=True); p.add_argument('--version', required=True); p.add_argument('--sha', required=True); p.set_defaults(func=lambda a: verify_live_browser(a.url, a.version, a.sha))
    p = commands.add_parser('resume'); p.add_argument('channel', choices=['build', 'browser', 'sourceforge', 'deploy']); p.add_argument('--tag'); p.add_argument('--run-id'); p.set_defaults(func=resume)
    args = parser.parse_args()
    if args.command == 'verify' and args.directory is None:
        with tempfile.TemporaryDirectory(prefix='dunecity-release-verify-') as directory:
            args.directory = Path(directory)
            args.func(args)
    else:
        args.func(args)


if __name__ == '__main__':
    main()
