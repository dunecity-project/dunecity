#!/usr/bin/env python3
"""Exercise real menu widgets with no public connection or shared user settings.
Test access is isolated to this diagnostic binary; production objects are unchanged."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description="Render real menus and check setup state in an isolated profile.")
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--audio-failure', action='store_true', help='Verify startup recovery when the audio driver cannot open')
parser.add_argument('--metaserver-maps', action='store_true', help='Read-only live catalogue and download smoke test in isolated profile')
parser.add_argument('--retina-resolution', action='store_true', help='Verify HD selection, Apply/reinitialization and fullscreen with Retina display data')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
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
for width, height in (((640,480),) if args.retina_resolution else (((1280, 720),) if args.metaserver_maps else ((640, 480), (854, 480), (1280, 720)))):
    profile = out / ('profile-' + str(width))
    profile.mkdir(exist_ok=True)
    (profile/'Dune City.ini').write_text('[Video]\nPhysical Width = '+str(width)+'\nPhysical Height = '+str(height)+'\nWidth = '+str(width)+'\nHeight = '+str(height)+'\nInterface Height = '+str(height)+'\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Menu tester\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='unavailable-test-driver' if args.audio_failure else 'dummy', MENU_PROBE_OUT=str(out), MENU_PROBE_WIDTH=str(width), MENU_PROBE_HEIGHT=str(height))
    if args.metaserver_maps: env['MENU_LIVE_MAPS']='1'
    if args.retina_resolution: env['MENU_RETINA_RESOLUTION']='1'
    logpath = out / ('run-' + str(width) + '.log')
    with logpath.open('w') as log:
        # The complete navigation fixture reloads every bundled graphics pack
        # and scans the large community maps at each of three screen sizes.
        # Keep the small Retina check bounded separately; 120 seconds truncated
        # progressing navigation runs before their final assertions on macOS.
        runtime_timeout = 720 if args.metaserver_maps else 120 if args.retina_resolution else 420
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=runtime_timeout)
    results = [line for line in logpath.read_text(errors="replace").splitlines() if 'MENU_PROBE_PASS:' in line]
    if len(results) != 1: raise RuntimeError('Missing menu test result; see '+str(logpath))
    if args.audio_failure:
        text = logpath.read_text(errors="replace")
        if 'Continuing with silent audio' not in text or 'Audio driver: dummy' not in text:
            raise RuntimeError('Missing silent audio recovery evidence; see '+str(logpath))
    print(results[0])
