#!/usr/bin/env python3
"""Compare exact A* routes/node counts with the pre-optimization implementation.

Links both implementations against the real engine, loads a save in an isolated
profile, and alternates timed searches without advancing or changing the match.
The baseline is read from git, not copied into production or approximated by a
test model. Requires the existing macOS Ninja build and its bundled game data.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import prepare_profile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--save', type=Path, required=True)
parser.add_argument('--reference-ref', default='12f5d46616d6736866668a73a7414d99119da220')
parser.add_argument('--profile-from', type=Path,
                    help='Copy settings and installed mods from this profile. A save made with a '
                         'workshop mod revision will not load against the bundled copy alone.')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=False)
subprocess.run(['python3', str(root/'scripts/check-build-deps.py'), str(build)], check=True)
ref = subprocess.check_output(['git','rev-parse','--verify',args.reference_ref+'^{commit}'], cwd=root, text=True).strip()
(out/'reference-commit.txt').write_text(ref+'\n')
for source, destination in [('include/AStarSearch.h','ReferenceAStarSearch.h'), ('src/AStarSearch.cpp','ReferenceAStarSearch.cpp')]:
    text = subprocess.check_output(['git','show',ref+':'+source], cwd=root, text=True)
    text = text.replace('ASTARSEARCH_H','REFERENCE_ASTARSEARCH_H').replace('AStarSearch','ReferenceAStarSearch')
    (out/destination).write_text(text)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target], text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1: raise RuntimeError('Main-menu injection point changed')
main = main.replace(needle,'int menuResult = runPathfindingProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position]+'#include "'+str(root/'tests/pathfinding/pathfinding-probe.inc')+'"\n'+main[position:]
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

app = out/'path-probe.app/Contents'
(app/'MacOS').mkdir(parents=True)
(app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/path-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(out/'probe-main.o') if arg.endswith('/main.cpp.o') else
        str(out/'candidate.o') if arg.endswith('/AStarSearch.cpp.o') else arg for arg in link]
link.append(str(out/'reference.o'))
with (out/'build.log').open('w') as log:
    for template, source, obj in [('main.cpp',out/'probe-main.cpp',out/'probe-main.o'),
                                 ('AStarSearch.cpp',root/'src/AStarSearch.cpp',out/'candidate.o'),
                                 ('AStarSearch.cpp',out/'ReferenceAStarSearch.cpp',out/'reference.o')]:
        subprocess.run(compile_source(template, source, obj), cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
profile = out/'profile'
active = prepare_profile(profile, args.profile_from)
if active: print('PATH_PROBE_PROFILE: active_mod='+active)
env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', PATH_PROBE_SAVE=str(args.save.resolve()))
with (out/'run.log').open('w') as log:
    # Every ground unit contributes seven queries and each is run twice per round:
    # three correct rounds over the 1712-unit city fixture measured ~103 seconds.
    subprocess.run([str(binary),'--window','--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
results = [line for line in (out/'run.log').read_text().splitlines() if 'PATH_PROBE_' in line]
if not any('PATH_PROBE_PASS:' in line for line in results): raise RuntimeError('Missing probe result')
print('\n'.join(results))
