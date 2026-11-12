#!/usr/bin/env python3
"""Exercise real menu widgets with no public connection or shared user settings.
Test access is isolated to this diagnostic binary; production objects are unchanged.
Like the native pointer gate and workspace test wrapper, preflight checks the disk
floor first and sweeps only when low; leased scratch cleanup ownership is unchanged."""
import argparse
from contextlib import contextmanager, nullcontext
import json
import os
from pathlib import Path
import shlex
import shutil
import socket
import signal
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
parser.add_argument('--pointer-mapping', action='store_true', help='Mini-only native Cocoa/SDL/real-menu mapping regression (moves only its own window, not the physical pointer)')
parser.add_argument('--runtime-app', type=Path, help='Use and verify this packaged app’s SDL libraries in the native pointer regression')
parser.add_argument('--pointer-sdl-only', action='store_true', help='Run only the small native SDL boundary phase, without game/mod/font initialization')
parser.add_argument('--tornie-family', action='store_true', help='Run only the six-edition campaign, custom lobby and map editor matrix')
parser.add_argument('--terrain-mix', action='store_true', help='Run only the 640x480 Random Skirmish terrain controls, saved choices and layout checks')
args = parser.parse_args()
if args.pointer_sdl_only and not args.pointer_mapping:
    parser.error('--pointer-sdl-only requires --pointer-mapping')
if args.pointer_mapping:
    if sys.platform != 'darwin' or 'air' in socket.gethostname().lower():
        print('Native pointer regression requires an authorized Mac desktop, never the MacBook Air.')
        sys.exit(77)
    if os.environ.get('DUNECITY_NATIVE_POINTER_TEST') != '1':
        print('Set DUNECITY_NATIVE_POINTER_TEST=1 on the Mini to allow own-window native Cocoa input.')
        sys.exit(77)
    def terminate_probe(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate_probe)
elif args.runtime_app:
    parser.error('--runtime-app requires --pointer-mapping')
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
# Match the workspace test guard: enforce the floor, sweeping owned stale
# scratch only if low instead of making every native UI check scan all scratch.
disk = require_free_space(sweep='if-low')
if args.pointer_mapping:
    print('POINTER_PREFLIGHT: '+json.dumps(disk, sort_keys=True), flush=True)
subprocess.run(['python3',str(root/'scripts/check-build-deps.py'),str(build)],check=True,cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
source, obj = out/'menu-main.cpp', out/'menu-main.o'
if args.pointer_sdl_only:
    source = root/'tests/menu/pointer-sdl-probe.cpp'
else:
    main = (root/'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed.')
    main = main.replace(needle,'int menuResult = runMenuProbe();')
    # Dummy-display probes retain the requested virtual width. The native
    # pointer fixture uses the production window-size handling unchanged.
    clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
    if main.count(clamp) != 1: raise RuntimeError('Window-size fixture injection point changed.')
    if not args.retina_resolution and not args.pointer_mapping:
        main = main.replace(clamp, '(void)displayIndex;')
    if args.pointer_mapping:
        seed = 'ModManager::instance().initialize();'
        if main.count(seed) != 1: raise RuntimeError('Vanilla fixture injection point changed.')
        main = main.replace(seed, 'initializePointerProbeVanilla();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = main[:position] + '#include "'+str(root/'tests/menu/menu-probe.inc')+'"\n' + main[position:]
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
if args.pointer_mapping:
    compile_command.extend(['-DDUNECITY_NATIVE_POINTER_PROBE=1', '-O0'])
app = out/'menu-probe.app/Contents'
(app/'MacOS').mkdir(parents=True, exist_ok=True)
if not (app/'Resources').exists(): (app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/menu-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
if args.pointer_sdl_only:
    # No game objects, assets or profile initialization in the first boundary
    # measurement; retain the exact game's SDK libraries and linker options.
    link = [arg for arg in link if not arg.endswith('.o')]
    link.insert(link.index('-o'), str(obj))
with (out/'build.log').open('w') as log:
    if args.pointer_mapping:
        native_obj = out/'mac-pointer-events.o'
        native_compile = list(compile_command)
        native_compile[native_compile.index('-o')+1] = str(native_obj)
        native_compile[native_compile.index('-c')+1] = str(root/'tests/menu/mac-pointer-events.mm')
        native_compile[native_compile.index('-c'):native_compile.index('-c')] = ['-x', 'objective-c++', '-fobjc-arc']
        subprocess.run(native_compile,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
        link.insert(link.index('-o'), str(native_obj))
        link.extend(['-framework', 'ApplicationServices'])
    subprocess.run(compile_command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    # Drive nested menus synchronously through their real widgets. Only this
    # diagnostic object gains a hook; the production menu loop stays unchanged.
    if not args.pointer_sdl_only:
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
if args.pointer_mapping:
    sizes = [(1440, 900)]

@contextmanager
def native_desktop():
    if not args.pointer_mapping:
        yield
        return
    import ctypes as c
    import ctypes.util
    c.CDLL(ctypes.util.find_library('AppKit'))
    objc = c.CDLL('/usr/lib/libobjc.A.dylib')
    objc.objc_getClass.argtypes, objc.objc_getClass.restype = [c.c_char_p], c.c_void_p
    objc.sel_registerName.argtypes, objc.sel_registerName.restype = [c.c_char_p], c.c_void_p
    send = c.CFUNCTYPE(c.c_void_p, c.c_void_p, c.c_void_p)(('objc_msgSend', objc))
    pid = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_void_p)(('objc_msgSend', objc))
    app_for_pid = c.CFUNCTYPE(c.c_void_p, c.c_void_p, c.c_void_p, c.c_int)(('objc_msgSend', objc))
    activate = c.CFUNCTYPE(c.c_bool, c.c_void_p, c.c_void_p, c.c_ulong)(('objc_msgSend', objc))
    sel = lambda name: objc.sel_registerName(name.encode())
    workspace = send(objc.objc_getClass(b'NSWorkspace'), sel('sharedWorkspace'))
    previous = pid(send(workspace, sel('frontmostApplication')), sel('processIdentifier'))
    # The diagnostic moves only its own window, never the physical pointer.
    # The supervisor therefore restores foreground ownership, not CG cursor state.
    (out/'desktop-before.json').write_text(json.dumps({'frontmostPid': previous, 'pointerMoved': False})+os.linesep)
    failed = False
    try:
        yield
    except BaseException:
        failed = True
        raise
    finally:
        app = app_for_pid(objc.objc_getClass(b'NSRunningApplication'), sel('runningApplicationWithProcessIdentifier:'), previous)
        if app and not activate(app, sel('activateWithOptions:'), 0):
            message = 'Mini foreground restoration request failed'
            print(message, file=sys.stderr, flush=True)
            if not failed: raise RuntimeError(message)
family_results = []
with (nullcontext(out) if args.pointer_sdl_only else scratch_dir('menu-probe')) as scratch:
    for width, height in ([(640,480)] if args.retina_resolution or args.terrain_mix else sizes):
        profile = scratch / ('profile-' + str(width) + 'x' + str(height))
        if not args.pointer_sdl_only:
            profile.mkdir()
            video = ('Physical Width = 5120\nPhysical Height = 2880\nWidth = 1066\nHeight = 600\nInterface Height = 600\nFullscreen = true\n'
                     if args.pointer_mapping else
                     'Physical Width = '+str(width)+'\nPhysical Height = '+str(height)+'\nWidth = '+str(width)+'\nHeight = '+str(height)+'\nInterface Height = '+str(height)+'\nFullscreen = false\n')
            (profile/'Dune City.ini').write_text('[Video]\n'+video+'[General]\nPlay Intro = false\nPlayer Name = Menu tester\n')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='unavailable-test-driver' if args.audio_failure else 'dummy', MENU_PROBE_OUT=str(out), MENU_PROBE_WIDTH=str(width), MENU_PROBE_HEIGHT=str(height))
        if args.pointer_mapping:
            env.pop('SDL_VIDEODRIVER', None)
            env['MENU_POINTER_MAPPING'] = '1'
            if args.runtime_app:
                runtime = args.runtime_app.resolve()
                frameworks = runtime/'Contents/Frameworks'
                if not (runtime/'Contents/MacOS/dunecity').is_file():
                    raise RuntimeError('Packaged runtime executable is missing: '+str(runtime))
                env['DYLD_LIBRARY_PATH'] = str(frameworks)
                env['MENU_POINTER_RUNTIME'] = str(args.runtime_app.resolve())
                dependencies = subprocess.check_output(['otool', '-L', str(runtime/'Contents/MacOS/dunecity')], text=True)
                names = [Path(line.strip().split(' (', 1)[0]).name for line in dependencies.splitlines()[1:]]
                dynamic = any(name == 'SDL2' or name.startswith(('libSDL2-', 'libSDL2.')) for name in names)
                env['MENU_POINTER_LINKAGE'] = 'dynamic' if dynamic else 'static'
                (out/'candidate-sdl-linkage.txt').write_text(env['MENU_POINTER_LINKAGE']+os.linesep+dependencies)
        if args.retina_resolution: env['MENU_RETINA_RESOLUTION']='1'
        if args.tornie_family: env['MENU_TORNIE_FAMILY']='1'
        if args.terrain_mix: env['MENU_TERRAIN_MIX']='1'
        logpath = out / ('run-' + str(width) + '.log')
        with logpath.open('w') as log:
            # The complete navigation fixture reloads every bundled graphics pack
            # and scans the large community maps at each of three screen sizes.
            runtime_timeout = 90 if args.pointer_mapping else 120 if args.retina_resolution else 420
            with native_desktop():
                command = [str(binary), '--showlog', '--non-interactive']
                if not args.pointer_mapping: command.insert(1, '--window')
                subprocess.run(command, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=runtime_timeout)
        text = logpath.read_text(errors="replace")
        results = [line for line in text.splitlines() if 'MENU_PROBE_PASS:' in line]
        if len(results) != 1: raise RuntimeError('Missing menu test result; see '+str(logpath))
        if not args.pointer_mapping and not args.retina_resolution and not args.terrain_mix and not args.tornie_family:
            versions = [line.split('MENU_VERSION_PASS: ', 1)[1]
                        for line in text.splitlines() if 'MENU_VERSION_PASS: ' in line]
            if len(versions) != 1 or not versions[0].startswith('App v'):
                raise RuntimeError('Missing main-menu display version; see '+str(logpath))
            display_version = versions[0][len('App v'):]
            if 'Starting DuneCity ' + display_version + ' on ' not in text:
                raise RuntimeError('Startup log and main-menu version disagree; see '+str(logpath))
        if not args.pointer_mapping and not args.retina_resolution and not args.terrain_mix:
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
        if not args.pointer_sdl_only:
            discard_profile_assets(profile)
            shutil.copytree(profile, out / ('profile-' + str(width)), dirs_exist_ok=True)
            shutil.rmtree(profile)
        print(results[0])
