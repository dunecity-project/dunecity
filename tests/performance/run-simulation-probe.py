#!/usr/bin/env python3
"""Run a fixed saved-game simulation in an isolated profile using the real engine.

Run before and after a change, then compare STATE lines and final save bytes.
Use --render-seconds and --profile-from to reproduce the ordinary graphical loop.
Requires the existing macOS Ninja build and its bundled game data.
"""
import argparse
import hashlib
import json
import struct
import shutil
import plistlib
import os
import configparser
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from probe_profile import prepare_profile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--save', type=Path, required=True)
parser.add_argument('--path-workers', default='', help='DUNECITY_PATH_WORKERS override (1=inline, 2/4/6=pool)')
parser.add_argument('--worker-delay-us', default='', help='Deterministic per-task worker delay, to reorder completions')
parser.add_argument('--cycles', type=int, default=2000)
parser.add_argument('--diagnostics', choices=('on','off'), help='Override the diagnostic logging setting in the private profile')
parser.add_argument('--render-seconds', type=int, default=0, help='Run the ordinary graphical game loop for this many seconds')
parser.add_argument('--profile-from', type=Path, help='Copy display/audio settings and active city mod from this profile')
parser.add_argument('--compare-dir', type=Path, help='Previous probe output; require matching checkpoints and saved state')
args = parser.parse_args()
if args.render_seconds and args.compare_dir: parser.error('Rendered runs do not have a fixed final cycle; use --cycles for exact state comparison')
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=False)
subprocess.run(['python3', str(root/'scripts/check-build-deps.py'), str(build)], check=True)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
pending = subprocess.check_output(['ninja','-C',str(build),'-n',target], text=True)
# CMake's CONFIGURE_DEPENDS glob verification is an always-dirty node, so a dry run
# always replans the glob check and the regeneration edge and can never report
# "no work to do". Ninja still plans every other dirty edge in the same run, so
# ignoring those two lines keeps the guard strict about stale objects.
regeneration = ('Entering directory', 'Re-checking globbed directories', 'Re-running CMake', 'no work to do')
stale = [line for line in pending.splitlines() if line.strip() and not any(text in line for text in regeneration)]
if stale: raise RuntimeError('Build the native game before probing; objects are stale: '+stale[0])
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target], text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1: raise RuntimeError('Main-menu injection point changed')
main = main.replace(needle,'int menuResult = runSimulationProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position]+'#include "'+str(root/'tests/performance/simulation-probe.inc')+'"\n'+main[position:]
(out/'probe-main.cpp').write_text(main)

def compile_source(template, source, obj):
    command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/'+template in line))
    for option in ('-include','-MT','-MF'):
        if option in command:
            position = command.index(option)
            del command[position:position+2]
    for option in ('-MD','-MMD'):
        if option in command: command.remove(option)
    command[command.index('-o')+1] = str(obj)
    command[command.index('-c')+1] = str(source)
    command.extend(['-I'+str(out), '-fno-access-control'])
    return command

app = out/'simulation-probe.app/Contents'
(app/'MacOS').mkdir(parents=True)
(app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/simulation-probe'
(app/'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'simulation-probe', 'CFBundleIdentifier':'net.dunecity.performance-probe', 'CFBundleName':'DuneCity Performance Probe', 'CFBundlePackageType':'APPL'}))
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(out/'probe-main.o') if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out/'build.log').open('w') as log:
    subprocess.run(compile_source('main.cpp', out/'probe-main.cpp', out/'probe-main.o'), cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
profile = out/'profile'
active = prepare_profile(profile, args.profile_from)
if active: print('SIM_PROBE_PROFILE: active_mod='+active)
if args.diagnostics:
    config=configparser.ConfigParser(interpolation=None,strict=False)
    config.read(profile/'Dune City.ini')
    if not config.has_section('General'): config.add_section('General')
    config.set('General','Diagnostic Logs','true' if args.diagnostics=='on' else 'false')
    with (profile/'Dune City.ini').open('w') as handle: config.write(handle)
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SIM_PROBE_SAVE=str(args.save.resolve()), SIM_PROBE_CYCLES=str(args.cycles), SIM_PROBE_OUTPUT=str(out/'final.dls'),
           DUNECITY_PATH_WORKERS=args.path_workers, DUNECITY_PATH_WORKER_DELAY_US=args.worker_delay_us)
if args.render_seconds:
    env['SIM_PROBE_RENDER_SECONDS']=str(args.render_seconds)
else:
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
with (out/'run.log').open('w') as log:
    subprocess.run([str(binary),'--window','--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=max(300,args.render_seconds+120))
results = [line for line in (out/'run.log').read_text().splitlines() if 'SIM_PROBE_' in line]
if not any('SIM_PROBE_PASS:' in line for line in results): raise RuntimeError('Missing probe result')
if args.diagnostics=='off':
    if list(profile.rglob('events.jsonl')) or list(profile.rglob('DuneCity-Performance.log')) or (out/'dunecity-crash.log').exists():
        raise RuntimeError('Disabled diagnostics still wrote a trace file')
    if 'SIM_PROBE_ERROR_REPORTING_CHECK:' not in (out/'run.log').read_text():
        raise RuntimeError('Disabled diagnostics suppressed error reporting')
elif args.diagnostics=='on':
    if not list(profile.rglob('events.jsonl')) or not list(profile.rglob('DuneCity-Performance.log')):
        raise RuntimeError('Enabled diagnostics did not capture the game')
print('\n'.join(results))

if args.compare_dir:
    reference = args.compare_dir.resolve()
    def states(directory):
        return [line for line in (directory/'run.log').read_text().splitlines() if 'SIM_PROBE_STATE:' in line]
    def saved_state(directory):
        data = (directory/'final.dls').read_bytes()
        # The release label is the only excluded field. Save-format version,
        # mod checksum, RNG, city layers, AI queues and all gameplay bytes match.
        length = struct.unpack_from('<I',data,8)[0]
        if length > 100 or not data[12:12+length].startswith(b'DuneCity'):
            raise RuntimeError('Unexpected save header')
        return data[:8]+data[12+length:]
    if states(reference) != states(out): raise RuntimeError('Simulation checkpoints changed')
    if saved_state(reference) != saved_state(out): raise RuntimeError('Saved gameplay state changed')
    report = {'reference':str(reference), 'checkpoints':len(states(out)),
              'saved_state_sha256':hashlib.sha256(saved_state(out)).hexdigest(),
              'identical_gameplay_state':True}
    (out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    print('SIM_PROBE_IDENTICAL:',json.dumps(report))
