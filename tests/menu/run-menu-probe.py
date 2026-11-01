#!/usr/bin/env python3
"""Exercise real menu widgets with no public connection or shared user settings.
Test access is isolated to this diagnostic binary; production objects are unchanged."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import discard_profile_assets, require_free_space, scratch_dir
parser = argparse.ArgumentParser(description="Render real menus and check setup state in an isolated profile.")
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--audio-failure', action='store_true', help='Verify startup recovery when the audio driver cannot open')
parser.add_argument('--size', action='append', metavar='WxH', help='Run at this window size instead of 640x480, 854x480 and 1280x720 (repeatable)')
parser.add_argument('--retina-resolution', action='store_true', help='Verify HD selection, Apply/reinitialization and fullscreen with Retina display data')
parser.add_argument('--tornie-family', action='store_true', help='Run only the six-edition campaign, custom lobby and map editor matrix')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
require_free_space()
subprocess.run(['python3',str(root/'scripts/check-build-deps.py'),str(build)],check=True,cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle,'int menuResult = runMenuProbe();')
# The dummy SDL desktop is only 1024 pixels wide. Keep the requested virtual
# window size so wide-screen probes exercise the real 1280-pixel menu layout.
clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
if main.count(clamp) != 1: raise RuntimeError('Window-size fixture injection point changed.')
if not args.retina_resolution:
    main = main.replace(clamp, '(void)displayIndex;')

main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "'+str(root/'tests/menu/menu-probe.inc')+'"\n' + main[position:]
source, obj = out/'menu-main.cpp', out/'menu-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include','-MT','-MF'):
    if option in compile_command:
        position = compile_command.index(option)
        del compile_command[position:position+2]
for option in ('-MD','-MMD'):
    if option in compile_command: compile_command.remove(option)
compile_command[compile_command.index('-o')+1] = str(obj)
compile_command[compile_command.index('-c')+1] = str(source)
compile_command.append('-fno-access-control')
app = out/'menu-probe.app/Contents'
(app/'MacOS').mkdir(parents=True, exist_ok=True)
if not (app/'Resources').exists(): (app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/menu-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out/'build.log').open('w') as log:
    subprocess.run(compile_command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    # Drive nested menus synchronously through their real widgets. Only this
    # diagnostic object gains a hook; the production menu loop stays unchanged.
    menu_base = (root/'src/Menu/MenuBase.cpp').read_text()
    menu_entry = 'int MenuBase::showMenu() {'
    if menu_base.count(menu_entry) != 1:
        raise RuntimeError('Nested menu fixture injection point changed.')
    menu_base = menu_base.replace(menu_entry, menu_entry + '''
    extern bool driveOnlineRandomSkirmishProbe(MenuBase&);
    if(driveOnlineRandomSkirmishProbe(*this)) return retVal;
''')
    menu_source, menu_obj = out/'menu-base.cpp', out/'menu-base.o'
    menu_source.write_text(menu_base)
    menu_compile = shlex.split(next(line for line in commands if ' -c ' in line and '/src/Menu/MenuBase.cpp' in line))
    for option in ('-include','-MT','-MF'):
        if option in menu_compile:
            position = menu_compile.index(option)
            del menu_compile[position:position+2]
    for option in ('-MD','-MMD'):
        if option in menu_compile: menu_compile.remove(option)
    menu_compile[menu_compile.index('-o')+1] = str(menu_obj)
    menu_compile[menu_compile.index('-c')+1] = str(menu_source)
    subprocess.run(menu_compile,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    link = [str(menu_obj) if arg.endswith('/Menu/MenuBase.cpp.o') else arg for arg in link]
    if args.retina_resolution:
        # The real menu code gets the captured MBA desktop/window/backing sizes.
        # Only this private object substitutes the display-query functions.
        options = shlex.split(next(line for line in commands if ' -c ' in line and '/src/Menu/OptionsMenu.cpp' in line))
        options[options.index('-o')+1] = str(out/'retina-options.o')
        for option in ('-include','-MT','-MF'):
            if option in options:
                position = options.index(option)
                del options[position:position+2]
        for option in ('-MD','-MMD'):
            if option in options: options.remove(option)
        options.extend(['-DSDL_GetDisplayBounds=retinaProbeDisplayBounds',
                        '-DSDL_GetWindowSize=retinaProbeWindowSize',
                        '-DSDL_GetRendererOutputSize=retinaProbeOutputSize'])
        subprocess.run(options,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        link = [str(out/'retina-options.o') if arg.endswith('/Menu/OptionsMenu.cpp.o') else arg for arg in link]
    subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
sizes = [tuple(int(v) for v in size.lower().split('x')) for size in args.size] if args.size else [(640, 480), (854, 480), (1280, 720)]
family_results = []
with scratch_dir('menu-probe') as scratch:
    for width, height in ([(640,480)] if args.retina_resolution else sizes):
        profile = scratch / ('profile-' + str(width) + 'x' + str(height))
        profile.mkdir()
        (profile/'Dune City.ini').write_text('[Video]\nPhysical Width = '+str(width)+'\nPhysical Height = '+str(height)+'\nWidth = '+str(width)+'\nHeight = '+str(height)+'\nInterface Height = '+str(height)+'\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Menu tester\n')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='unavailable-test-driver' if args.audio_failure else 'dummy', MENU_PROBE_OUT=str(out), MENU_PROBE_WIDTH=str(width), MENU_PROBE_HEIGHT=str(height))
        if args.retina_resolution: env['MENU_RETINA_RESOLUTION']='1'
        if args.tornie_family: env['MENU_TORNIE_FAMILY']='1'
        logpath = out / ('run-' + str(width) + '.log')
        with logpath.open('w') as log:
            # The complete navigation fixture reloads every bundled graphics pack
            # and scans the large community maps at each of three screen sizes.
            runtime_timeout = 120 if args.retina_resolution else 420
            subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=runtime_timeout)
        text = logpath.read_text(errors="replace")
        results = [line for line in text.splitlines() if 'MENU_PROBE_PASS:' in line]
        if len(results) != 1: raise RuntimeError('Missing menu test result; see '+str(logpath))
        if not args.retina_resolution:
            rows = [dict(field.split('=', 1) for field in line.split('TORNIE_MENU_PASS: ', 1)[1].split())
                    for line in text.splitlines() if 'TORNIE_MENU_PASS: ' in line]
            if {row['mod'] for row in rows} != {'Tornie', 'TornieLite', 'Jericho', 'JerichoLite', 'ArcaKev', 'ArcaKevLite'} or len(rows) != 6:
                raise RuntimeError('Incomplete Tornie-family menu matrix; see '+str(logpath))
            for row in rows:
                family_results.append(dict(width=width, height=height,
                                           **{key: int(value) if value.isdigit() else value for key, value in row.items()}))
                print('TORNIE_MENU_PASS: ' + ' '.join(key+'='+value for key, value in row.items()))
            (out/'tornie-family-results.json').write_text(json.dumps(family_results, indent=2)+'\n')
        if args.audio_failure:
            if 'Continuing with silent audio' not in text or 'Audio driver: dummy' not in text:
                raise RuntimeError('Missing silent audio recovery evidence; see '+str(logpath))
        discard_profile_assets(profile)
        shutil.copytree(profile, out / ('profile-' + str(width)), dirs_exist_ok=True)
        shutil.rmtree(profile)
        print(results[0])
