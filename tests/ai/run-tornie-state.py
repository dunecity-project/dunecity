#!/usr/bin/env python3
"""Real-command Tornie mechanics, deterministic continuation, and authentic 9877 saves.

Compile only a private diagnostic main against an existing macOS Ninja build.
--source-root/--build-dir may name a read-only 9877 baseline: no build products,
profiles, cache entries, or dependency records are written to that checkout.
Use --mod Tornie/vanilla there to create authentic saves, then --legacy-save
with build-tornie to check the old-format defaults and continued simulation.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import scratch_dir, require_free_space, discard_profile_assets, copy_revisions, prune_runs, MINIMAL_INI

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=root)
parser.add_argument('--build-dir', type=Path, default=root / 'build-tornie')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--repeatable', action='store_true', help='Keep each CTest invocation in a fresh run directory')
parser.add_argument('--section', choices=('all', 'mechanics', 'busy', 'aircraft'), default=os.environ.get('TORNIE_STATE_SECTION', 'all'))
parser.add_argument('--mod', choices=('Tornie', 'vanilla'), default='Tornie')
parser.add_argument('--legacy-save', type=Path)
parser.add_argument('--save-profile', type=Path, help='Profile holding the exact mod revision pinned by --legacy-save')
parser.add_argument('--scratch-dir', type=Path, help='Already leased scratch directory for a multi-step compatibility run')
parser.add_argument('--keep-profile-assets', action='store_true', help='Keep leased assets until a later compatibility load')
parser.add_argument('--prebuilt-probe', type=Path)
args = parser.parse_args()
source_root, build, out = args.source_root.resolve(), args.build_dir.resolve(), args.output_dir.resolve()
if args.repeatable:
    out.mkdir(parents=True, exist_ok=True)
    prune_runs(out, 'run-', 2)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=out))
else:
    out.mkdir(parents=True, exist_ok=False)
require_free_space()
if args.legacy_save and not args.legacy_save.is_file():
    parser.error('--legacy-save must exist')
if args.keep_profile_assets and not args.scratch_dir:
    parser.error('--keep-profile-assets requires an explicitly leased --scratch-dir')
if args.prebuilt_probe:
    binary = args.prebuilt_probe.resolve()
    compiled_fixture = binary.parents[3] / 'tornie-state-probe.inc'
else:
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = (source_root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu diagnostic injection point changed')
    main = main.replace(needle, 'int menuResult = runTornieStateProbeDriver();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    compiled_fixture = out / 'tornie-state-probe.inc'
    compiled_fixture.write_bytes((root / 'tests/ai/tornie-state-probe.inc').read_bytes())
    main = main[:position] + '#include "' + str(compiled_fixture) + '"\n' + main[position:]
    source = out / 'tornie-state-main.cpp'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option)
            del cc[i:i+2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    obj = out / 'tornie-state-main.o'
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc += ['-fno-access-control']
    app = out / 'tornie-state-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/tornie-state-probe'
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&')+1:]
    link = link[:link.index('&&')]
    link[link.index('-o')+1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    # Same mutex as the shared build; never ask Ninja to rebuild the baseline.
    with open('/tmp/dunecity-tornie-546.build.lock', 'a') as lock, (out / 'build.log').open('w') as log:
        fcntl.flock(lock, fcntl.LOCK_EX)
        subprocess.run(['nice', '-n', '10', *cc], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['nice', '-n', '10', *link], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out / 'build-commands.json').write_text(json.dumps({'compile': cc, 'link': link}, indent=2))

provenance = {'sourceRoot': str(source_root), 'build': str(build), 'probe': str(binary),
              'mod': args.mod, 'legacySave': str(args.legacy_save) if args.legacy_save else None,
              'probeSha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'fixtureSha256': hashlib.sha256(compiled_fixture.read_bytes()).hexdigest() if compiled_fixture.is_file() else None,
              'section': args.section}
provenance['sourceCommit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source_root, text=True).strip()
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
with scratch_dir('tornie-state-qa', explicit=args.scratch_dir) as scratch:
    profile = scratch / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(MINIMAL_INI)
    if args.save_profile:
        copy_revisions(args.save_profile, profile, [args.legacy_save] if args.legacy_save else [])
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy',
               SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy',
               TORNIE_STATE_OUT=str(out), TORNIE_STATE_MOD=args.mod, TORNIE_STATE_SECTION=args.section, DUNECITY_PATH_WORKERS='1')
    if args.legacy_save:
        env['TORNIE_STATE_LEGACY_SAVE'] = str(args.legacy_save.resolve())
    with (out / 'run.log').open('w') as log:
        done = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                              stdout=log, stderr=subprocess.STDOUT, timeout=300)
    (out / 'settings.ini').write_text((profile / 'Dune City.ini').read_text())
    text = (out / 'run.log').read_text(errors='replace')
    result = [line for line in text.splitlines() if 'TORNIE_GAMEPLAY_' in line]
    saves = {}
    for save in out.glob('*.dls'):
        data = save.read_bytes()
        saves[save.name] = {'bytes': len(data), 'magic': struct.unpack_from('<I', data)[0],
                            'version': struct.unpack_from('<I', data, 4)[0],
                            'sha256': hashlib.sha256(data).hexdigest()}
    (out / 'summary.json').write_text(json.dumps({'exitCode': done.returncode, 'events': result, 'saves': saves}, indent=2) + '\n')
    if done.returncode or not any('TORNIE_GAMEPLAY_PASS:' in line or 'TORNIE_GAMEPLAY_BASELINE:' in line for line in result):
        raise RuntimeError('Real Tornie state probe failed: ' + str(out / 'run.log'))
    if not args.keep_profile_assets:
        discard_profile_assets(profile)
print('Tornie state probe passed. Evidence: ' + str(out))
