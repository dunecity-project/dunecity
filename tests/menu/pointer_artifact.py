"""Producer-bound static SDL pointer fixtures; consumers never configure or compile.

The trust root is the externally pinned publication manifest and its Actions
artifact digest, not a version string or a receipt supplied beside a binary.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import tarfile
import zipfile

from probe_fixture import file_hash, source_fingerprint
from source_provenance import pinned_source_fingerprint
from probe_process import check_output

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'bin/dunecity.app/Contents/MacOS/dunecity'
ARCHIVE = 'pointer-fixture.tar.gz'
MAX_PAYLOAD = 128 * 1024 * 1024
PORTS = {'libSDL2.a': 'sdl2', 'libSDL2main.a': 'sdl2',
         'libSDL2_mixer.a': 'sdl2-mixer', 'libSDL2_ttf.a': 'sdl2-ttf'}


def require(condition, reason):
    if not condition:
        raise RuntimeError('POINTER_PREREQUISITE_BLOCKED: ' + reason)


def encoded(value):
    return (json.dumps(value, sort_keys=True) + '\n').encode()


def value_hash(value):
    return hashlib.sha256(encoded(value)).hexdigest()


def checksum(value, length=64):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{%d}' % length, value) is not None


def read_json(path):
    # Duplicate keys must not give a receipt two interpretations.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=unique)


def link_command(commands):
    matches = [shlex.split(line) for line in commands if ' -o ' + TARGET + ' ' in line]
    require(len(matches) == 1, 'ambiguous candidate link recipe')
    command = matches[0]
    if '&&' in command:
        command = command[command.index('&&') + 1:]
    if '&&' in command:
        command = command[:command.index('&&')]
    require('-o' in command and command[command.index('-o') + 1] == TARGET,
            'unrecognized candidate link recipe')
    return command


def clean_source(root, build, output, *, expected_commit=None, status_output=None):
    sha = check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    require(checksum(sha, 40), 'source must be a full committed SHA')
    require(expected_commit is None or checksum(expected_commit, 40), 'selected source must be a full committed SHA')
    context = dict(sourceRoot=str(Path(root).resolve()), sourceCommit=sha, expectedSourceCommit=expected_commit or sha,
                   buildRoot=str(Path(build).resolve()), outputRoot=str(Path(output).resolve()))

    def blocked(code, reason, **evidence):
        detail = dict(context, status='blocked-prerequisite', reasonCode=code, **evidence)
        if status_output is not None:
            Path(status_output).write_bytes(encoded(detail))
        require(False, reason + '; ' + json.dumps(detail, sort_keys=True))

    if expected_commit is not None and sha != expected_commit:
        blocked('source-head-mismatch', 'producer/consumer HEAD differs from the selected source')
    fingerprint = source_fingerprint(root, build, output)
    expected = pinned_source_fingerprint(root, sha)
    if fingerprint != expected:
        # No file contents or environment secrets: preserve the failed input's
        # actual roots/hashes and bounded nonignored paths, never normalize it.
        status = check_output(['git', 'status', '--porcelain=v1', '--untracked-files=all'], cwd=root, text=True).splitlines()
        blocked('source-fingerprint-mismatch', 'producer/consumer source is not the exact committed input',
                worktreeFingerprint=fingerprint, committedFingerprint=expected,
                dirtyPaths=status[:64], dirtyPathCount=len(status))
    return dict(commit=sha, fingerprint=fingerprint)


def bundle_files(app):
    app = Path(app).resolve(strict=True)
    result = {}
    # Reuse the fixture helper's cycle/dangling-link checks. Bundle links may
    # resolve only within this candidate; extraction never imports these links.
    from probe_fixture import _runtime_files
    for path in _runtime_files(app):
        require(path.resolve(strict=True).is_relative_to(app), 'candidate link escapes its app: ' + str(path))
        result[path.relative_to(app).as_posix()] = file_hash(path)
    require('Contents/MacOS/dunecity' in result, 'candidate executable is missing')
    return result


def spdx_hash(document, name, prefix):
    matches = [item for item in document['files']
               if item['fileName'] == './' + name and item['SPDXID'].startswith(prefix)]
    require(len(matches) == 1, 'missing/ambiguous SPDX file identity: ' + name)
    hashes = [item['checksumValue'] for item in matches[0]['checksums'] if item['algorithm'] == 'SHA256']
    require(len(hashes) == 1 and checksum(hashes[0]), 'missing SHA256 in SPDX: ' + name)
    return hashes[0]


def validate_sdk(sdk, manifest, production, fixture=None):
    """Cross-check installed bytes against vcpkg's binary/recipe/source receipts."""
    versions = {item['name']: item['version'] for item in manifest['overrides']}
    require(sdk['baseline'] == manifest['builtin-baseline'] and checksum(sdk['baseline'], 40),
            'static SDK baseline differs from source')
    libraries = sdk['archives']
    require(libraries and {item['port'] for item in libraries.values()} == {'sdl2', 'sdl2-mixer', 'sdl2-ttf'},
            'incomplete full-menu static SDL SDK')
    for command in [production] + ([fixture] if fixture is not None else []):
        linked = [arg for arg in command if Path(arg).name.startswith(('libSDL', 'SDL', '-lSDL')) and not arg.endswith('.o')]
        require(set(linked) == set(libraries), 'foreign, implicit or different SDL in link recipe')
    for path, record in libraries.items():
        relative, port = record['relative'], record['port']
        require(Path(path).name in PORTS and PORTS[Path(path).name] == port
                and relative == 'lib/' + Path(path).name
                and str(Path(path).parent.parent) == sdk['prefix']
                and Path(sdk['prefix']).name == 'arm64-osx', 'uncertain static archive provenance')
        evidence = sdk['ports'][port]
        document, abi = evidence['spdx'], evidence['abiInfo']
        packages = {item['SPDXID']: item for item in document['packages']}
        recipe, binary = packages['SPDXRef-port'], packages['SPDXRef-binary']
        require(recipe['name'] == port and recipe['versionInfo'] == versions[port]
                and re.fullmatch(r'git\+https://github.com/[Mm]icrosoft/vcpkg@[0-9a-f]{40}', recipe['downloadLocation']),
                'unanchored vcpkg port recipe/version')
        require(binary['name'] == port + ':arm64-osx'
                and binary['versionInfo'] == hashlib.sha256(abi.encode()).hexdigest(), 'vcpkg binary ABI receipt mismatch')
        rows = dict(line.split(' ', 1) for line in abi.splitlines())
        require(rows.get('triplet') == 'arm64-osx' and rows.get('triplet_abi'), 'missing static SDK triplet ABI')
        recipes = [item for item in document['files'] if item['SPDXID'].startswith('SPDXRef-port-file-')]
        names = {item['fileName'][2:] for item in recipes}
        require({'portfile.cmake', 'vcpkg.json'}.issubset(names), 'missing port build recipe hashes')
        for name in names:
            require(rows.get(name) == spdx_hash(document, name, 'SPDXRef-port-file-'), 'port recipe/ABI identity mismatch')
        resources = [item for item in document['packages'] if item['SPDXID'].startswith('SPDXRef-resource-')]
        require(resources and all(item.get('downloadLocation') not in (None, 'NONE', 'NOASSERTION')
                and any(entry['algorithm'] in ('SHA256', 'SHA512')
                        and checksum(entry['checksumValue'], 64 if entry['algorithm'] == 'SHA256' else 128)
                        for entry in item.get('checksums', [])) for item in resources), 'unhashed static SDK upstream source')
        require(checksum(record['sha256']) and record['sha256'] == spdx_hash(document, relative, 'SPDXRef-binary-file-'),
                'actual static archive differs from installed SPDX receipt')
    core = sdk['ports']['sdl2']['spdx']
    require(sdk['headerSha256'] == spdx_hash(core, 'include/SDL2/SDL_version.h', 'SPDXRef-binary-file-')
            and sdk['version'] == versions['sdl2'], 'SDL header/version provenance mismatch')
    return sdk['version']


def producer_snapshot(root, build, output, *, status_output=None):
    root, build = Path(root).resolve(), Path(build).resolve()
    selected = os.environ.get('DUNECITY_CANDIDATE_SOURCE')
    require(checksum(selected, 40), 'normal Mac producer requires its explicit selected candidate source')
    source = clean_source(root, build, output, expected_commit=selected, status_output=status_output)
    commands = check_output(['ninja', '-C', str(build), '-t', 'commands', TARGET], text=True).splitlines()
    production = link_command(commands)
    archives = {str((build / arg).resolve(strict=True)): arg for arg in production if arg.endswith('.a') and 'SDL' in Path(arg).name}
    require(archives and all(Path(path).name in PORTS for path in archives), 'candidate is not the supported explicit static SDL SDK')
    prefix = Path(next(iter(archives))).parent.parent
    sdk = dict(prefix=str(prefix), baseline=read_json(root / 'vcpkg.json')['builtin-baseline'], archives={}, ports={})
    for path in archives:
        port = PORTS[Path(path).name]
        sdk['archives'][path] = dict(port=port, relative=Path(path).relative_to(prefix).as_posix(), sha256=file_hash(path))
        if port not in sdk['ports']:
            share = prefix / 'share' / port
            sdk['ports'][port] = dict(spdx=read_json(share / 'vcpkg.spdx.json'), abiInfo=(share / 'vcpkg_abi_info.txt').read_text())
    normalized = [str((build / arg).resolve()) if arg.endswith('.a') else arg for arg in production]
    header = prefix / 'include/SDL2/SDL_version.h'
    numbers = [re.search(r'#define\s+SDL_' + part + r'\s+(\d+)', header.read_text())[1]
               for part in ('MAJOR_VERSION', 'MINOR_VERSION', 'PATCHLEVEL')]
    sdk.update(version='.'.join(numbers), headerSha256=file_hash(header))
    validate_sdk(sdk, read_json(root / 'vcpkg.json'), normalized)
    context = {str(build / name): file_hash(build / name) for name in ('CMakeCache.txt', 'build.ninja')}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if re.match(r'^CMAKE_(C_COMPILER|CXX_COMPILER|TOOLCHAIN_FILE):', line):
            path = Path(line.split('=', 1)[1])
            require(path.is_file(), 'configured compiler/toolchain is unavailable')
            context[str(path)] = file_hash(path)
    for line in commands:
        args = shlex.split(line)
        for arg in args[:args.index('-c')] if '-c' in args else []:
            if Path(arg).is_absolute() and Path(arg).is_file() and os.access(arg, os.X_OK):
                context[arg] = file_hash(arg)
        if '-isysroot' in args:
            sysroot = Path(args[args.index('-isysroot') + 1])
            settings = [sysroot / name for name in ('SDKSettings.json', 'SDKSettings.plist') if (sysroot / name).is_file()]
            require(settings, 'missing compiler platform SDK identity')
            context.update({str(path): file_hash(path) for path in settings})
    return dict(source=source, commands=commands, productionLink=production,
                normalizedLink=normalized, sdk=sdk, context=context)


def producer_identity():
    require(os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('GITHUB_JOB') == 'build-macos',
            'diagnostic export is restricted to the normal Mac producer')
    values = {key: os.environ.get(name) for key, name in dict(repository='GITHUB_REPOSITORY', runId='GITHUB_RUN_ID',
              runAttempt='GITHUB_RUN_ATTEMPT', sourceCommit='GITHUB_SHA', workflowCommit='GITHUB_WORKFLOW_SHA',
              workflowRef='GITHUB_WORKFLOW_REF').items()}
    require(all(values.values()) and checksum(values['sourceCommit'], 40)
            and values['workflowCommit'] == values['sourceCommit']
            and values['runId'].isdigit() and values['runAttempt'].isdigit()
            and int(values['runId']) > 0 and int(values['runAttempt']) > 0
            and values['workflowRef'].startswith(values['repository'] + '/.github/workflows/build.yml@'),
            'missing or ambiguous producing workflow identity')
    values['checkedOutWorkflowSha256'] = file_hash(ROOT / '.github/workflows/build.yml')
    values['frontendSha256'] = file_hash(ROOT / 'scripts/macos-ci-lane.sh')
    return values


def package_executable_hash(package):
    """Bind the receipt's candidate Mach-O to an actual member of the final ZIP."""
    with zipfile.ZipFile(package) as archive:
        entries = [item for item in archive.infolist()
                   if item.filename == 'dunecity.app/Contents/MacOS/dunecity']
        require(len(entries) == 1 and not entries[0].is_dir()
                and not stat.S_ISLNK(entries[0].external_attr >> 16), 'missing/ambiguous final package Mach-O')
        digest = hashlib.sha256()
        with archive.open(entries[0]) as stream:
            for chunk in iter(lambda: stream.read(1 << 20), b''):
                digest.update(chunk)
        return digest.hexdigest()


def export_fixture(root, build, output, snapshot_path, binary, candidate, package, destination):
    before = read_json(snapshot_path)
    require(before == producer_snapshot(root, build, output), 'source/compiler/static SDK changed across candidate production')
    recipe = read_json(binary.parents[3] / 'recipe.json')
    fixture_link = [str((Path(build) / arg).resolve()) if arg.endswith('.a') else arg for arg in recipe['link']]
    validate_sdk(before['sdk'], read_json(Path(root) / 'vcpkg.json'), before['normalizedLink'], fixture_link)
    require([arg for arg in recipe['link'] if arg.endswith('.a')] == [arg for arg in before['productionLink'] if arg.endswith('.a')]
            and recipe['link'][0] == before['productionLink'][0], 'fixture changed candidate link/compiler inputs')
    seal = read_json(binary.parents[3] / 'manifest.json')
    require(seal['binary'] == file_hash(binary) and seal['identity']['source'] == before['source']['fingerprint']
            and seal['identity']['variant'] == dict(pointer=True, sdl_only=False, retina=False), 'not the full sealed pointer fixture')
    files = bundle_files(candidate)
    require(package_executable_hash(package) == files['Contents/MacOS/dunecity'],
            'candidate Mach-O differs from the actual final package ZIP member')
    receipt = dict(schema=1, kind='producer-static-sdl-full-menu-pointer-fixture', actualPackageExecutable=False,
                   producer=producer_identity(), source=before['source'], inputs=before,
                   inputsSha256=value_hash(before), fixture=dict(sha256=file_hash(binary), recipe=recipe,
                       inputHashes={str(Path(path).resolve()): value for path, value in seal['identity']['inputs'].items()
                                    if path.endswith(('.o', '.a', 'CMakeCache.txt', 'build.ninja'))}),
                   candidate=dict(package=Path(package).name, packageSha256=file_hash(package), files=files))
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    receipt_path = destination / 'receipt.json'
    receipt_path.write_bytes(encoded(receipt))
    with tarfile.open(destination / ARCHIVE, 'w:gz') as archive:
        for path, name in ((binary, 'menu-probe'), (receipt_path, 'receipt.json')):
            item = archive.gettarinfo(str(path), arcname=name)
            item.uid = item.gid = item.mtime = 0
            item.uname = item.gname = ''
            item.mode = 0o755 if name == 'menu-probe' else 0o644
            with Path(path).open('rb') as stream:
                archive.addfile(item, stream)
    return receipt, file_hash(receipt_path)


def authenticate_artifact(archive, manifest_path, manifest_sha256, source_commit):
    require(checksum(manifest_sha256) and file_hash(manifest_path) == manifest_sha256,
            'publication manifest differs from its external pin')
    manifest = read_json(manifest_path)
    require(checksum(source_commit, 40) and manifest['schema'] == 1 and manifest['sourceCommit'] == source_commit
            and manifest['producerWorkflowPath'] == '.github/workflows/build.yml', 'wrong-source or untrusted publication manifest')
    name = 'DuneCity-macOS-pointer-fixture-%s-%s' % (source_commit, manifest['runAttempt'])
    matches = [item for item in manifest['artifacts'] if item['name'] == name]
    require(len(matches) == 1, 'named producing snapshot has no unique pointer diagnostic; coverage remains blocked')
    artifact = matches[0]
    require(isinstance(artifact['id'], int) and artifact['id'] > 0 and artifact['expired'] is False
            and artifact['size_in_bytes'] == Path(archive).stat().st_size
            and artifact['digest'] == 'sha256:' + file_hash(archive), 'Actions diagnostic artifact is expired, foreign or tampered')
    return manifest, artifact


def extract_artifact(archive, work):
    work = Path(work)
    require(not work.exists() and not work.is_symlink(), 'diagnostic extraction destination already exists')
    parent = work.parent.resolve(strict=True)
    require(parent.stat().st_uid == os.getuid() and not parent.stat().st_mode & 0o022,
            'diagnostic extraction requires private owned leased scratch')
    work.mkdir(mode=0o700)
    payload = work / ARCHIVE
    with zipfile.ZipFile(archive) as source:
        entries = source.infolist()
        require(len(entries) == 1 and entries[0].filename == ARCHIVE and not entries[0].is_dir()
                and not stat.S_ISLNK(entries[0].external_attr >> 16)
                and 0 < entries[0].file_size <= MAX_PAYLOAD, 'unsafe or oversized Actions diagnostic envelope')
        with source.open(entries[0]) as stream, payload.open('xb') as target:
            shutil.copyfileobj(stream, target, length=1 << 20)
    with tarfile.open(payload) as source:
        entries = source.getmembers()
        require(sorted(item.name for item in entries) == ['menu-probe', 'receipt.json']
                and all(item.isfile() and not item.issym() and not item.islnk() for item in entries)
                and sum(item.size for item in entries) <= MAX_PAYLOAD, 'unsafe or oversized pointer diagnostic members')
    # Use the existing extraction policy: no links, traversal, archive ownership,
    # timestamps, imported permissions or overwrites, including on Python 3.9.
    spec = importlib.util.spec_from_file_location('pointer_release_extraction', ROOT / 'scripts/release-automation.py')
    release = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release)
    release.extract_browser(payload, work / 'payload')
    return work / 'payload'


def validate_receipt(receipt, manifest, source, candidate_files, binary_hash):
    require(receipt['schema'] == 1 and receipt['kind'] == 'producer-static-sdl-full-menu-pointer-fixture'
            and receipt['actualPackageExecutable'] is False and receipt['source'] == source
            and source['commit'] == manifest['sourceCommit'], 'fixture/source/coverage identity mismatch')
    producer = receipt['producer']
    require(producer['repository'] == manifest['repository']
            and str(producer['runId']) == str(manifest['runId'])
            and str(producer['runAttempt']) == str(manifest['runAttempt'])
            and producer['sourceCommit'] == manifest['producerSourceCommit']
            and producer['workflowCommit'] == manifest['producerSourceCommit']
            and producer['workflowRef'].startswith(manifest['repository'] + '/.github/workflows/build.yml@')
            and producer['checkedOutWorkflowSha256'] == file_hash(ROOT / '.github/workflows/build.yml')
            and producer['frontendSha256'] == file_hash(ROOT / 'scripts/macos-ci-lane.sh'),
            'fixture belongs to a different/untrusted producer')
    inputs = receipt['inputs']
    require(receipt['inputsSha256'] == value_hash(inputs) and inputs['source'] == source
            and inputs['context'] and all(checksum(value) for value in inputs['context'].values()), 'unsealed producer input context')
    require(receipt['fixture']['sha256'] == binary_hash
            and receipt['fixture']['inputHashes'] and all(checksum(value) for value in receipt['fixture']['inputHashes'].values()),
            'tampered fixture bytes or unsealed link inputs')
    require(all(receipt['fixture']['inputHashes'].get(path) == record['sha256']
                for path, record in inputs['sdk']['archives'].items()), 'fixture seal did not hash the actual SDL archive inputs')
    candidate = receipt['candidate']
    require(candidate['files'] == candidate_files
            and candidate['packageSha256'] == manifest['sha256'].get(candidate['package'])
            and re.fullmatch(r'DuneCity-[0-9.]+-macOS\.zip', candidate['package']), 'different candidate Mach-O, resources or package bytes')
    fixture = receipt['fixture']['recipe']['link']
    build = Path(receipt['fixture']['recipe']['buildRoot'])
    normalized = [str((build / arg).resolve()) if arg.endswith('.a') else arg for arg in fixture]
    require(link_command(inputs['commands']) == inputs['productionLink'], 'candidate recipe is not its recorded Ninja command')
    production = [str((build / arg).resolve()) if arg.endswith('.a') else arg for arg in inputs['productionLink']]
    require(production == inputs['normalizedLink'], 'candidate archive resolution changed')
    return validate_sdk(inputs['sdk'], read_json(ROOT / 'vcpkg.json'), production, normalized)


def consume_fixture(archive, manifest_path, manifest_sha256, source_commit, candidate, work, build, output):
    manifest, metadata = authenticate_artifact(archive, manifest_path, manifest_sha256, source_commit)
    source = clean_source(ROOT, build, output, expected_commit=source_commit)
    payload = extract_artifact(archive, work)
    receipt = read_json(payload / 'receipt.json')
    validate_receipt(receipt, manifest, source, bundle_files(candidate), file_hash(payload / 'menu-probe'))
    app = Path(work) / 'menu-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    binary = app / 'MacOS/menu-probe'
    (payload / 'menu-probe').rename(binary)
    binary.chmod(0o700)
    (app / 'Resources').symlink_to(Path(candidate).resolve() / 'Contents/Resources')
    dependencies = check_output(['otool', '-L', str(binary)], text=True)
    require(not any('SDL' in Path(line.strip().split(' (', 1)[0]).name for line in dependencies.splitlines()[1:]),
            'producer static fixture imports a foreign dynamic SDL')
    output = Path(output)
    shutil.copyfile(payload / 'receipt.json', output / 'pointer-producer-receipt.json')
    (output / 'pointer-artifact-reference.json').write_bytes(encoded(dict(publicationManifestSha256=manifest_sha256,
        artifact=metadata, receiptSha256=file_hash(payload / 'receipt.json'), actualPackageExecutable=False)))
    return binary, receipt, file_hash(payload / 'receipt.json')


def runtime_environment(env, candidate, proof=None, receipt_sha256=None):
    env = {key: value for key, value in env.items()
           if not key.startswith(('DYLD_', 'MENU_POINTER_'))}
    runtime = Path(candidate).resolve(strict=True)
    dependencies = check_output(['otool', '-L', str(runtime / 'Contents/MacOS/dunecity')], text=True)
    names = [Path(line.strip().split(' (', 1)[0]).name for line in dependencies.splitlines()[1:]]
    dynamic = any(name == 'SDL2' or name.startswith(('libSDL2-', 'libSDL2.')) for name in names)
    if proof is not None:
        require(not dynamic and checksum(receipt_sha256), 'verified static receipt cannot authorize a dynamic candidate')
        env.update(MENU_POINTER_VERIFIED_RECEIPT=receipt_sha256, MENU_POINTER_SDL_VERSION=proof['inputs']['sdk']['version'])
    env.update(MENU_POINTER_MAPPING='1', MENU_POINTER_RUNTIME=str(runtime),
               MENU_POINTER_LINKAGE='dynamic' if dynamic else 'static', DYLD_LIBRARY_PATH=str(runtime / 'Contents/Frameworks'))
    require(dynamic or proof is not None, 'static SDL candidate requires its own producer-built diagnostic; no local SDK fallback')
    return env, dependencies


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['snapshot'])
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--status-output', type=Path, help='Existing gate-status.json for bounded source-admission failure evidence')
    args = parser.parse_args()
    producer_identity()
    args.output.write_bytes(encoded(producer_snapshot(ROOT, args.build_dir, args.output.parent, status_output=args.status_output)))
