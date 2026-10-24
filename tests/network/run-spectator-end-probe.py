#!/usr/bin/env python3
"""A decided online match, watched by a network spectator, played by separate processes.

Builds a diagnostic copy whose main menu is replaced by tests/network/spectator-end-probe.inc.
Private Game.cpp observations record actual production verdict/plate calls; every other core
object is reused and production source/objects remain unchanged. Starts local PHP signaling and
runs Host, Partner and Viewer over real WebRTC, each in the real Game::runMainLoop.
The winner's tanks hunt the loser's troopers, so the simulation decides the match itself; the
viewer joins mid-game as a spectator. Cases:

  end            the host leaves after its six-second end message and the statistics
  decision       the host quits at the very cycle the match is decided
  lag-end        the viewer hangs 20 s from the decision: it is about 7 s behind when the host leaves
  lag-decision   the viewer hangs 25 s from the decision and the host quits at it (25 s behind)
  slow           the viewer runs at under half speed from the start (falls further behind all game)
  natives        Dune City mod with the map's sandworms (the hidden Natives house)
  feature-combo  saved Natives crime troops shoot down a plane; its real crash decides the game;
                 late Viewer hangs 25 s from cycle 1200, with causal verdict/departure receipts
  feature-decision  the same fixture, with host departure exactly at the decision

Every case requires identical decision/final cycles and digests, a clean observer end and the
correct winner plate/statistics. --baseline reports instead of failing.

Mac modes remain local or --remote ROLE=SSHHOST. For a native Windows Viewer (or Partner), first
prepare the combined source once through the shared cluster, then select its existing registered
tree (w0..w3; never a fresh build tree). The parent MUST wait for all PC-eligible queued/running
work and preparations to drain and hold the global project coordinator for the entire command.
--windows-idle-window attests that parent-managed admission window; the driver never drains,
cancels, or manipulates the scheduler. Any active unit/preparation or guard fails closed.

  agent-workspace run --project dunecity --owner PARENT_OWNER --wait -- \\
    python3 tests/network/run-spectator-end-probe.py --build-dir build-int \\
    --output-dir /Users/stefan/Documents/projects/outputs/crossplay-COMBINED \\
    --remote Viewer=dunecity-win --windows-tree w0 --windows-source-id PREPARED_SID \\
    --windows-idle-window --cases feature-combo,feature-decision

Replace Viewer with Partner to exercise a native Windows player. Both may be specified together.
The driver reads the registered access/configuration from machines.json, reserves the idle PC
with a token-owned PAUSE, links only a private diagnostic main against that tree's existing native
objects, and releases only its own reservation. No Wine, global runner edits, production lobby,
OS/security changes, or foreign-process termination. Any guard/reservation failure is a failure,
never a passing skip. This command is preparation/verification for the parent, not evidence of a run.
"""
import argparse
import atexit
import ast
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import runpy
import sys
import tempfile
import time
import uuid

RENDEZVOUS = ('room', 'init', 'running', 'Host-decided', 'Viewer-stall-start', 'Viewer-stall-end',
              'Host-verdict-armed', 'Host-verdict-event', 'Host-departed')


def source_fingerprint(source):
    """Exact cross-platform native/assets inputs, not a commit or a guessed compatible build."""
    digest = hashlib.sha256()
    paths = [source / name for name in ('CMakeLists.txt', 'vcpkg.json', 'resource.rc')]
    for name in ('src', 'include', 'config', 'mods', 'data', 'cmake', 'IDE/VC'):
        paths.extend(p for p in (source / name).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and not p.name.endswith(('.pyc', '.bak')))
    paths.extend(source / name for name in (
        'tests/ai/run-campaign-balance.py', 'tests/ai/windows-probe-compat.inc',
        'tests/network/run-spectator-end-probe.py', 'tests/network/spectator-end-probe.inc'))
    for path in sorted(paths, key=lambda p: p.relative_to(source).as_posix()):
        digest.update(path.relative_to(source).as_posix().encode() + b'\0')
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def injected_main(source):
    main = (source / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the test injection point.')
    main = main.replace(needle, 'int menuResult = runSpectatorEndProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                        'if(false && shouldPlayIntro && (bFirstInit==true))')
    if sys.platform == 'win32':
        flags = 'Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;'
        if main.count(flags) != 1:
            raise RuntimeError('Windows dummy renderer injection point changed')
        main = main.replace(flags, 'Uint32 rendererFlags = SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE;')
    includes = ['tests/network/spectator-end-probe.inc']
    if sys.platform == 'win32':
        includes.insert(0, 'tests/ai/windows-probe-compat.inc')
    pos = main.index('int main(')
    return main[:pos] + ''.join('#include "' + (source / p).as_posix() + '"\n' for p in includes) + main[pos:]


def injected_game(source):
    """Observe actual production verdict/plate calls in a private diagnostic object only."""
    code = (source / 'src/Game.cpp').read_text()
    for method in ('setGameWon', 'setGameLost', 'setWatchedGameOver'):
        start = code.index('void Game::' + method + '(')
        end = code.index('\n}\n', start) + 2
        block = code[start:end]
        assignment = 'won = false;' if method != 'setGameWon' else 'won = true;'
        if block.count(assignment) != 1:
            raise RuntimeError('Production verdict observation point changed: ' + method)
        block = block.replace(assignment, 'spectatorProbeVerdict(gameCycleCount, false);\n'
                              + ('    ' if method == 'setWatchedGameOver' else '        ') + assignment)
        if method == 'setWatchedGameOver':
            point = '    watchedTimeUp = timeUp;'
        else:
            point = next(line for line in block.splitlines() if 'finishedLevelCycle = gameCycleCount;' in line)
        if block.count(point) != 1:
            raise RuntimeError('Production applied-verdict observation point changed: ' + method)
        block = block.replace(point, point + '\n'
                              + ('    ' if method == 'setWatchedGameOver' else '        ')
                              + 'spectatorProbeVerdict(gameCycleCount, true);')
        code = code[:start] + block + code[end:]
    points = {
        # Identity guards tie the observed white text/frame to the actual rendering arguments.
        '        sdl2::texture_ptr pFinishMessageTexture = pFontManager->createTextureWithText(message.c_str(), COLOR_WHITE, finishFontSize);':
            '        sdl2::texture_ptr pFinishMessageTexture = pFontManager->createTextureWithText(message.c_str(), COLOR_WHITE, finishFontSize);',
        '                renderDrawRect(renderer, &border, frame);':
            '                renderDrawRect(renderer, &border, frame);',
        '        if(watchedWinnerTeam > 0) {':
            '        Uint32 probeFrame = 0;\n        bool probeFramed = false;\n        if(watchedWinnerTeam > 0) {',
        '            const Uint32 frame = watchedResultColor(1);':
            '            const Uint32 frame = watchedResultColor(1);\n            probeFrame = frame; probeFramed = true;',
        '        SDL_RenderCopy(renderer, pFinishMessageTexture.get(), nullptr, &drawLocation);':
            '        SDL_RenderCopy(renderer, pFinishMessageTexture.get(), nullptr, &drawLocation);\n'
            '        spectatorProbePlate(message.c_str(), COLOR_WHITE, probeFrame, probeFramed);',
    }
    for needle, replacement in points.items():
        if code.count(needle) != 1:
            raise RuntimeError('Production plate observation point changed: ' + needle)
        code = code.replace(needle, replacement)
    return ('extern "C" void spectatorProbeVerdict(unsigned, bool);\n'
            'extern "C" void spectatorProbePlate(const char*, unsigned, unsigned, bool);\n' + code)


def mac_probe_command(commands, original, source, obj):
    cc = shlex.split(next(line for line in commands if ' -c ' in line and '/src/' + original in line))
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option); del cc[i:i + 2]
    cc = [a for a in cc if a not in ('-MD', '-MMD')]
    if cc and Path(cc[0]).name in ('ccache', 'sccache'):
        cc = cc[1:]
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    return cc + ['-fno-access-control']


def native_worker(request):
    """Windows-only RPC, executed from the registered source; never changes that source/build."""
    if sys.platform != 'win32':
        raise RuntimeError('The native Windows worker requires real Windows')
    home = Path(request['home']).resolve()
    tree = home / 'trees' / request['tree']
    output = home / 'diagnostics' / request['token']
    pause = home / 'PAUSE'
    token = 'spectator-end:' + request['token']
    # Pin the already-published node release, rather than importing its CLI wrapper as __main__.
    wrapper = ast.parse((home / 'node.py').read_text())
    calls = [n for n in ast.walk(wrapper) if isinstance(n, ast.Call)
             and isinstance(n.func, ast.Attribute) and n.func.attr == 'run_path']
    if len(calls) != 1 or not calls[0].args or not isinstance(calls[0].args[0], ast.Constant):
        raise RuntimeError('Unsupported published node entry point')
    release = Path(calls[0].args[0].value)
    node = runpy.run_path(str(release))
    cfg = node['read_json'](release.with_name('config.json'))
    if not cfg or cfg.get('machine') != 'pc':
        raise RuntimeError('Missing registered PC node configuration')
    os.environ.update(node['read_json'](release.with_name('env.json'), {}))
    os.environ.update(cfg.get('env', {}))

    def owned():
        return pause.is_file() and pause.read_text() == token

    def state():
        st = node['status'](home, cfg)
        held = st.get('guard', {})
        if held.get('state') == 'held' and not (owned() and held.get('reason') == 'PAUSE file'):
            raise RuntimeError('PC runner guard remains held: ' + held.get('reason', 'unknown reason'))
        if owned():
            st['pause'] = [p for p in st['pause'] if Path(p).resolve() != pause]
        # Do not inherit the runner guard's stale PAUSE reason for OUR reservation. All actual
        # battery/game/thermal/load/manual-pause inputs are still checked through hold_reason.
        reason = node['hold_reason'](st, cfg, 99 if st['foreign_cpu'] > cfg['guard'].get('foreign_limit', 400) else 0)
        if (reason or st.get('legacy') or st.get('mem_pressure', 1) >= 4
                or st['free_gb'] < 25 or st.get('mem_avail_gb', 0) < 1.5):
            raise RuntimeError('PC guard: ' + (reason or str(st)))
        return st

    def idle():
        if node['unit_records'](home) or any(node['record_alive'](node['read_json'](p))
                                             for p in (home / 'trees').glob('*/prep.pid')):
            raise RuntimeError('PC must drain its registered units/preparations before crossplay reservation')
        if node['status'](home, cfg).get('games_total'):
            raise RuntimeError('PC has an existing game; parent-managed idle window is not available')

    def pinned():
        if not owned():
            raise RuntimeError('PC reservation lost or manually paused')
        saved = node['read_json'](tree / 'state.json', {})
        if saved.get('sid') != request['sid'] or saved.get('state') != 'ready':
            raise RuntimeError('Registered Windows tree is not ready for the requested source id')
        state()

    def guarded(command, log, cwd, env=None, timeout=420):
        pinned()
        child = subprocess.Popen(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                 creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        record = {'pid': child.pid, 'boot': node['boot_time']()}
        pidfile = output / ('child-' + str(child.pid) + '.json')
        node['write_json'](pidfile, record)
        try:
            deadline = time.monotonic() + timeout
            while child.poll() is None:
                pinned()
                if (output / 'cancel').exists() or time.monotonic() >= deadline:
                    raise RuntimeError('Owned diagnostic cancelled or timed out')
                time.sleep(2)
            if child.returncode:
                raise subprocess.CalledProcessError(child.returncode, command)
        finally:
            if child.poll() is None:
                node['kill_tree'](record)  # Only this worker's recorded child and descendants.
                child.wait(timeout=15)
            pidfile.unlink(missing_ok=True)

    action = request['action']
    if action == 'reserve':
        st = state()
        if st.get('mem_avail_gb', 0) < 1.5 + 0.7 * request['slots']:
            raise RuntimeError('PC lacks the registered per-peer memory headroom')
        idle()
        if source_fingerprint(tree / 'src') != request['fingerprint']:
            raise RuntimeError('Windows source/assets differ from the Mac source; prepare the combined snapshot first')
        output.mkdir(parents=True, exist_ok=False)
        try:
            with pause.open('x') as f:
                f.write(token)
            # Let the scheduler observe PAUSE and finish any already-in-flight tick before
            # accepting the reservation. Never register a fake unit or write tree metadata.
            time.sleep(8)
            idle()
            pinned()
            node['write_json'](output / '.diagnostic-owner.json', {
                'owner': token, 'created': time.time(), 'source_id': request['sid'],
                'failure_retention_s': 72 * 3600})
        except BaseException:
            if owned():
                pause.unlink()
            raise
        return {}
    if action == 'release':
        (output / 'cancel').touch()
        deadline = time.monotonic() + 20
        while any(node['record_alive'](node['read_json'](p)) for p in output.glob('child-*.json')):
            if time.monotonic() > deadline:
                raise RuntimeError('Owned diagnostic child did not stop; reservation retained')
            time.sleep(1)
        if owned():
            pause.unlink()
        return {}
    pinned()
    if action == 'prepare':
        source, build = tree / 'src', tree / 'build'
        # Reuse the real MSVC/clang-cl command parser WITHOUT importing its argument parser/run.
        helper = source / 'tests/ai/run-campaign-balance.py'
        parsed = ast.parse(helper.read_text())
        names = {'windows_argv', 'windows_probe_commands'}
        selected = ast.Module(body=[n for n in parsed.body if isinstance(n, ast.FunctionDef)
                                   and n.name in names], type_ignores=[])
        scope = dict(vars(sys.modules[__name__]))
        import ctypes
        scope['ctypes'] = ctypes
        exec(compile(selected, str(helper), 'exec'), scope)
        cc, link, runtime = scope['windows_probe_commands'](build)
        private_source = output / 'spectator-end-main.cpp'
        private_source.write_text(injected_main(source), encoding='utf-8')
        game_source, game_obj = output / 'spectator-end-game.cpp', output / 'Game.obj'
        game_source.write_text(injected_game(source), encoding='utf-8')
        game_cc = [a.replace('@SOURCE@', str(game_source)).replace('@OBJECT@', str(game_obj)) for a in cc]
        game_inputs = [a for a in link if a.replace('\\', '/').endswith('/Game.cpp.obj')]
        if len(game_inputs) != 1:
            raise RuntimeError('Expected exactly one production Game.cpp.obj in diagnostic link')
        link = [str(game_obj) if a == game_inputs[0] else a for a in link]
        binary = output / 'spectator-end-probe.exe'
        names = {'@SOURCE@': str(private_source), '@OBJECT@': str(output / 'main.obj'),
                 '@BINARY@': str(binary), '@STAGING@': str(output)}
        def fill(arg):
            for old, new in names.items():
                arg = arg.replace(old, new)
            return arg
        with (output / 'build.log').open('w') as log:
            guarded([fill(a) for a in cc], log, build)
            guarded([fill(a) for a in game_cc], log, build)
            guarded([fill(a) for a in link], log, build)
        # Runtime DLLs are private copies; large asset directories are read-only NTFS junctions.
        # No executable, object, library, source or profile is written into the shared tree.
        for path in runtime.iterdir():
            target = output / path.name
            if path.is_file() and path.suffix.lower() in ('.dll', '.pak', '.ini', '.ttf'):
                shutil.copy2(path, target)
            elif path.is_dir():
                subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(target), str(path)],
                               check=True, stdout=subprocess.DEVNULL)
        return {'binary': str(binary)}
    directory = output / request['case'] / request['role']
    directory.mkdir(parents=True, exist_ok=True)
    if action == 'exchange':
        for name, encoded in request.get('files', {}).items():
            if Path(name).name != name:
                raise RuntimeError('Invalid rendezvous filename')
            path = directory / name
            if not path.exists():
                tmp = path.with_suffix(path.suffix + '.tmp')
                tmp.write_bytes(base64.b64decode(encoded))
                os.replace(tmp, path)
        return {p.name: base64.b64encode(p.read_bytes()).decode() for p in directory.iterdir()
                if p.is_file() and (p.name in RENDEZVOUS or p.name in ('Viewer-stalled', 'Viewer-feature-checkpoint')
                                   or p.name.endswith(('-result', '-decided', '-features'))
                                   or request.get('collect') and p.suffix in ('.log', '.png'))}
    if action == 'run':
        profile = directory / 'profile'
        profile.mkdir(exist_ok=True)
        (profile / 'Dune City.ini').write_text(
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\n[General]\nPlay Intro = false\n')
        env = dict(os.environ, **request['env'], SE_OUT=str(directory), DUNECITY_USERDIR=str(profile))
        # Check tunnel readiness, not a guessed delay. The endpoint remains loopback-only.
        import urllib.request
        deadline = time.monotonic() + 20
        while True:
            try:
                urllib.request.urlopen(env['SE_ENDPOINT'], timeout=2).close()
                break
            except urllib.error.HTTPError:
                break  # PHP answered: the tunnel is up (the root need not be a route).
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError('Windows loopback signaling tunnel did not become ready')
                time.sleep(0.2)
        with (directory / (request['role'] + '.log')).open('w') as log:
            guarded([str(output / 'spectator-end-probe.exe'), '--window', '--showlog'], log, directory, env)
        # Keep failures for inspection; successful regenerated profile bulk is disposable.
        for relative in ('mods', 'workshop/revisions'):
            shutil.rmtree(profile / relative, ignore_errors=True)
        return {}
    raise RuntimeError('Unknown native worker action: ' + action)


if len(sys.argv) == 3 and sys.argv[1] == '--native-worker':
    print(json.dumps(native_worker(json.loads(base64.b64decode(sys.argv[2])))))
    sys.exit(0)

root = Path(__file__).resolve().parents[2]
CASES = {
    'end':          dict(),
    'decision':     dict(SE_HOST_LEAVE='decision'),
    'lag-end':      dict(SE_STALL_MS='20000'),
    'lag-decision': dict(SE_STALL_MS='25000', SE_HOST_LEAVE='decision'),
    'slow':         dict(SE_SLOW_MS='35'),
    'natives':      dict(SE_MOD='dunecity'),
    'feature-combo': dict(SE_MOD='dunecity', SE_FEATURE_COMBO='1', SE_JOIN_AT='300',
                          SE_STALL_MS='25000', SE_STALL_AT='cycle:1200'),
    'feature-decision': dict(SE_MOD='dunecity', SE_FEATURE_COMBO='1', SE_JOIN_AT='300',
                             SE_STALL_MS='25000', SE_STALL_AT='cycle:1200', SE_HOST_LEAVE='decision'),
}
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import default_build_dir, managed_profile
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--cases', default='end,decision,lag-end,lag-decision')
parser.add_argument('--host-wins', action='store_true', help='The host plays the winning side (default: the partner wins)')
parser.add_argument('--baseline', action='store_true', help='Report what happens; do not fail on the spectator checks')
parser.add_argument('--binary', type=Path, help='Use this already built probe executable')
parser.add_argument('--remote', action='append', default=[], metavar='ROLE=SSHHOST',
                    help='Run ROLE (Host, Partner or Viewer) on SSHHOST: the probe app is copied there, the '
                         'signaling service is reached through an ssh tunnel and the files are relayed.')
parser.add_argument('--remote-dir', default='/tmp/dunecity-spectator-end',
                    help='Directory on remote Macs for the probe app and outputs')
parser.add_argument('--machines', type=Path, default=Path.home() / '.local/share/dunecity-runner/lib/machines.json',
                    help='Registered shared runner access/configuration (not a host override)')
parser.add_argument('--windows-tree', choices=('w0', 'w1', 'w2', 'w3'),
                    help='Existing prepared PC tree to reserve read-only; required for a Windows remote')
parser.add_argument('--windows-source-id', help='Exact prepared cluster sid in that Windows tree')
parser.add_argument('--windows-idle-window', action='store_true',
                    help='Parent attests no PC-eligible queued work and holds the global project coordinator')
args = parser.parse_args()
cases = [c for c in args.cases.split(',') if c]
for c in cases:
    if c not in CASES: parser.error('unknown case ' + c)
remote = dict(item.split('=', 1) for item in args.remote)
for role in remote:
    if role not in ('Host', 'Partner', 'Viewer'): parser.error('unknown role ' + role)
machines = json.loads(args.machines.read_text()) if args.machines.is_file() else {}
pc = machines.get('pc', {})
windows_roles = {role for role, host in remote.items() if host in ('pc', pc.get('ssh'))}
if windows_roles:
    if not pc.get('windows') or not pc.get('ssh') or not pc.get('python'):
        parser.error('Native Windows requires the registered pc entry in machines.json')
    if not args.windows_tree or not args.windows_source_id:
        parser.error('Native Windows requires --windows-tree and --windows-source-id for the combined prepared source')
    if not args.windows_idle_window:
        parser.error('Native Windows requires the parent-managed --windows-idle-window')
    if 'Host' in windows_roles:
        parser.error('This crossplay driver requires a Mac host; Windows supports Viewer and Partner')
    for role in windows_roles:
        remote[role] = pc['ssh']
if not (os.environ.get('PHP_BIN') or shutil.which('php8.3') or shutil.which('php')
        or Path('/opt/homebrew/bin/php').exists()):
    if windows_roles:
        parser.error('Crossplay requires local PHP signaling; it cannot be skipped')
    print('SKIPPED: the signaling service needs PHP (brew install php)')
    sys.exit(77)
if not os.environ.get('PHP_BIN') and not shutil.which('php') and Path('/opt/homebrew/bin/php').exists():
    os.environ['PHP_BIN'] = '/opt/homebrew/bin/php'

build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-spectator-end-'))
out.mkdir(parents=True, exist_ok=True)
run_token = 'se-' + uuid.uuid4().hex[:10]
if any((out / case).exists() for case in cases):
    out = out / ('run-' + run_token)
    out.mkdir()
# Never overwrite an earlier remote run's app, evidence, settings or assets.
args.remote_dir = args.remote_dir.rstrip('/') + '/' + run_token

app = out / 'spectator-end-probe.app/Contents'
binary = args.binary.resolve() if args.binary else app / 'MacOS/spectator-end-probe'
if not args.binary:
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = injected_main(root)
    source, obj = out / 'spectator-end-probe-main.cpp', out / 'spectator-end-probe-main.o'
    source.write_text(main)
    cc = mac_probe_command(lines, 'main.cpp', source, obj)
    game_source, game_obj = out / 'spectator-end-probe-game.cpp', out / 'spectator-end-probe-game.o'
    game_source.write_text(injected_game(root))
    game_cc = mac_probe_command(lines, 'Game.cpp', game_source, game_obj)
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    if not (app / 'Resources').exists():
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    game_inputs = [a for a in link if a.endswith('/Game.cpp.o')]
    if len(game_inputs) != 1:
        raise RuntimeError('Expected exactly one production Game.cpp.o in diagnostic link')
    link = [str(obj) if a.endswith('/main.cpp.o') else str(game_obj) if a == game_inputs[0] else a for a in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(game_cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)


def probe_map(natives, host_wins, features=False):
    """Habbanya-Autumn (51x31) without bases: ten hunting tanks against three troopers."""
    text = (root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini').read_text()
    head, units = text.split('[UNITS]', 1)
    rest = units[units.index('\n['):] if '\n[' in units else ''
    # The map's Player2 is the first house of the game (the host's Atreides), Player1 the partner's.
    strong, weak = ('Player2', 'Player1') if host_wins else ('Player1', 'Player2')
    if features:
        # Keep the Natives worms safely on sand, and the battle on a small rock island.
        # Two ordinary windtraps keep both houses alive until the real aircraft crash.
        map_section = '[MAP]\nSizeX=51\nSizeY=31\n' + ''.join(
            '%03d=%s\n' % (y, '+' * 51 if y < 5 or y >= 26 else '+' * 5 + '%' * 41 + '+' * 5)
            for y in range(31))
        head = re.sub(r'\[MAP\].*?(?=\n\[)', map_section, head, flags=re.S)
        return (head + '[UNITS]\nID001=Fremen,Sandworm,256,1,64,Area Guard\n'
                'ID002=Fremen,Sandworm,256,1579,64,Area Guard\n'
                '[STRUCTURES]\nID003=%s,Windtrap,256,%d\nID004=%s,Windtrap,256,%d\n'
                % (strong, 8 * 51 + 8, weak, 15 * 51 + 25))
    rows = []
    n = 1
    for i in range(10):
        x, y = 3 + i % 5, 16 + i // 5
        rows.append('ID%03d=%s,Tank,256,%d,64,Hunt' % (n, strong, y * 51 + x)); n += 1
    for x in (40, 41, 42):
        rows.append('ID%03d=%s,Trooper,256,%d,64,Area Guard' % (n, weak, 13 * 51 + x)); n += 1
    if natives:
        rows += ['ID%03d=Fremen,Sandworm,256,1579,64,Area Guard' % n, 'ID%03d=Fremen,Sandworm,256,1,64,Area Guard' % (n + 1)]
    return head + '[UNITS]\n' + '\n'.join(rows) + '\n' + rest


def ssh(host, command, **kw):
    return subprocess.run(['ssh', '-o', 'BatchMode=yes', host, command], **kw)


def stage_remote(host):
    """Copy the probe app (with its game data) to a remote Mac once."""
    rd = args.remote_dir
    ssh(host, 'mkdir -p ' + shlex.quote(rd + '/app/Contents/MacOS'), check=True)
    subprocess.run(['rsync', '-a', str(binary), host + ':' + rd + '/app/Contents/MacOS/'], check=True)
    subprocess.run(['rsync', '-aL', '--delete', str(build / 'bin/dunecity.app/Contents/Resources') + '/',
                    host + ':' + rd + '/app/Contents/Resources/'], check=True)
    # Sparkle is found through @executable_path/../Frameworks there (the build's rpath is local).
    subprocess.run(['rsync', '-a', '--delete', str(build / 'bin/dunecity.app/Contents/Frameworks') + '/',
                    host + ':' + rd + '/app/Contents/Frameworks/'], check=True)

windows_base = dict(home=pc.get('home'), tree=args.windows_tree, sid=args.windows_source_id,
                    token=run_token, slots=len(windows_roles))
windows_reserved = False


def windows_command(action, **fields):
    request = dict(windows_base, action=action, **fields)
    encoded = base64.b64encode(json.dumps(request).encode()).decode()
    script = '%s/trees/%s/src/tests/network/run-spectator-end-probe.py' % (pc['home'], args.windows_tree)
    quote = lambda s: "'" + s.replace("'", "''") + "'"
    return "& %s %s --native-worker %s" % (quote(pc['python']), quote(script), quote(encoded))


def windows_rpc(action, **fields):
    done = ssh(pc['ssh'], windows_command(action, **fields), capture_output=True, text=True)
    if done.returncode:
        raise RuntimeError('Windows %s failed (%s); diagnostic output %s/diagnostics/%s:\n%s%s'
                           % (action, done.returncode, pc['home'], run_token, done.stdout, done.stderr))
    return json.loads(done.stdout)


def release_windows():
    global windows_reserved
    if windows_reserved:
        windows_rpc('release')
        windows_reserved = False


def exchange_windows(case, role, case_out, collect=False):
    files = {name: base64.b64encode((case_out / name).read_bytes()).decode()
             for name in SHARED if (case_out / name).exists()}
    returned = windows_rpc('exchange', case=case, role=role, files=files, collect=collect)
    for name, encoded in returned.items():
        target = case_out / name
        if not target.exists():
            tmp = target.with_suffix(target.suffix + '.tmp')
            tmp.write_bytes(base64.b64decode(encoded))
            os.replace(tmp, target)


sys.path.insert(0, str(root / 'tools/p2p-signaling/test'))
from test_signaling import ServiceFixture

SHARED = RENDEZVOUS
failures = []
summary = []
if windows_roles:
    fingerprint = source_fingerprint(root)
    windows_rpc('reserve', fingerprint=fingerprint)
    windows_reserved = True
    atexit.register(release_windows)
    prepared = windows_rpc('prepare')
    (out / 'windows-peer.json').write_text(json.dumps(
        dict(windows_base, ssh=pc['ssh'], roles=sorted(windows_roles),
             fingerprint=fingerprint, **prepared), indent=2) + '\n')
for host in {host for role, host in remote.items() if role not in windows_roles}:
    stage_remote(host)
for case in cases:
    env_case = CASES[case]
    case_out = out / case
    if case_out.exists():
        raise RuntimeError('Refusing to replace earlier probe evidence: ' + str(case_out))
    case_out.mkdir()
    (case_out / 'map.ini').write_text(probe_map(case == 'natives', args.host_wins, 'SE_FEATURE_COMBO' in env_case))
    service = ServiceFixture()
    processes, logs, tunnels, profiles = {}, [], [], []
    try:
        if windows_roles:
            log = (case_out / 'Windows-tunnel.log').open('w'); logs.append(log)
            tunnels.append(subprocess.Popen(
                ['ssh', '-o', 'BatchMode=yes', '-o', 'ExitOnForwardFailure=yes', '-N',
                 '-R', '127.0.0.1:%d:127.0.0.1:%d' % (service.port, service.port), pc['ssh']],
                stdout=log, stderr=subprocess.STDOUT))
        for role in ('Host', 'Partner', 'Viewer'):
            env = {'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SE_ROLE': role,
                   'SE_ENDPOINT': 'http://127.0.0.1:%d' % service.port}
            env.update(env_case)
            env['SE_HOST_WINS'] = str(int(args.host_wins))
            if role in windows_roles:
                exchange_windows(case, role, case_out)
                log = (case_out / (role + '.ssh.log')).open('w'); logs.append(log)
                processes[role] = subprocess.Popen(
                    ['ssh', '-o', 'BatchMode=yes', pc['ssh'], windows_command('run', case=case, role=role, env=env)],
                    stdout=log, stderr=subprocess.STDOUT)
                continue
            if role in remote:
                host = remote[role]
                rdir = '%s/%s-%s' % (args.remote_dir, case, role)
                ssh(host, 'mkdir -p %s/profile' % shlex.quote(rdir), check=True)
                if role == 'Host':
                    subprocess.run(['scp', '-q', str(case_out / 'map.ini'), host + ':' + rdir + '/map.ini'], check=True)
                env.update(SE_OUT=rdir, DUNECITY_USERDIR=rdir + '/profile')
                # The probe reaches the signaling service on its own loopback, through the tunnel.
                command = ' '.join('%s=%s' % (k, shlex.quote(v)) for k, v in env.items())
                command += ' %s/app/Contents/MacOS/%s --window --showlog > %s/%s.log 2>&1' % (
                    args.remote_dir, binary.name, rdir, role)
                log = (case_out / (role + '.ssh.log')).open('w'); logs.append(log)
                processes[role] = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', '-o', 'ExitOnForwardFailure=yes',
                    '-R', '%d:127.0.0.1:%d' % (service.port, service.port), host, command],
                    stdout=log, stderr=subprocess.STDOUT)
            else:
                profile = case_out / ('profile-' + role)
                profile.mkdir()
                context = managed_profile(profile)
                context.__enter__()
                profiles.append(context)
                env.update(SE_OUT=str(case_out), DUNECITY_USERDIR=str(case_out / ('profile-' + role)))
                log = (case_out / (role + '.log')).open('w'); logs.append(log)
                processes[role] = subprocess.Popen([str(binary), '--window', '--showlog'], cwd=case_out,
                    env=dict(os.environ, **env), stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 420
        while time.monotonic() < deadline:
            # Relay the rendezvous files between machines (local roles share case_out).
            for role, host in remote.items():
                if role in windows_roles:
                    exchange_windows(case, role, case_out)
                    continue
                rdir = '%s/%s-%s' % (args.remote_dir, case, role)
                listing = ssh(host, 'ls %s' % shlex.quote(rdir), capture_output=True, text=True).stdout.split()
                for name in listing:
                    if (name in SHARED or name.endswith(('-result', '-decided', '-features'))
                            or name in ('Viewer-feature-checkpoint', 'Viewer-stalled')) and not (case_out / name).exists():
                        subprocess.run(['scp', '-q', host + ':' + rdir + '/' + name, str(case_out / (name + '.tmp'))], check=True)
                        (case_out / (name + '.tmp')).rename(case_out / name)
                for name in SHARED:
                    if (case_out / name).exists() and name not in listing:
                        subprocess.run(['scp', '-q', str(case_out / name), host + ':' + rdir + '/' + name + '.tmp'], check=True)
                        ssh(host, 'mv %s/%s.tmp %s/%s' % (rdir, name, rdir, name), check=True)
            if any(t.poll() is not None for t in tunnels):
                raise RuntimeError('Windows signaling tunnel exited; see Windows-tunnel.log')
            if all(p.poll() is not None for p in processes.values()): break
            time.sleep(0.5 if remote else 0.2)
        else:
            raise RuntimeError(case + ': timed out; logs in ' + str(case_out))
        for role, host in remote.items():
            if role in windows_roles:
                exchange_windows(case, role, case_out, collect=True)
                continue
            rdir = '%s/%s-%s' % (args.remote_dir, case, role)
            subprocess.run(['scp', '-q', host + ':' + rdir + '/' + role + '.log', str(case_out / (role + '.log'))])
            subprocess.run(['scp', '-q', host + ':' + rdir + '/*.png', str(case_out)], stderr=subprocess.DEVNULL)
        codes = {role: p.returncode for role, p in processes.items()}
    finally:
        for p in processes.values():
            if p.poll() is None: p.terminate()
        for p in processes.values():
            try: p.wait(timeout=5)
            except subprocess.TimeoutExpired: p.kill()
        for tunnel in tunnels:
            tunnel.terminate()
            try: tunnel.wait(timeout=5)
            except subprocess.TimeoutExpired: tunnel.kill()
        for log in logs: log.close()
        service.stop()
        if sys.exc_info()[0] is not None:
            for context in reversed(profiles):
                context.__exit__(*sys.exc_info())

    def result(role):
        path = case_out / (role + '-result')
        if not path.exists(): return {}
        return dict(line.split('=', 1) for line in path.read_text().splitlines() if '=' in line)
    host, partner, viewer = result('Host'), result('Partner'), result('Viewer')
    decided = {r: (case_out / (r + '-decided')).read_text().strip() if (case_out / (r + '-decided')).exists() else None
               for r in ('Host', 'Partner', 'Viewer')}
    winner = 'Atreides' if args.host_wins else 'Harkonnen'
    expected = 'House %s has won' % winner
    # A host that quits at the decision may leave before the partner (one lockstep cycle behind)
    # has simulated it; the partner's game then ends without it.
    if env_case.get('SE_HOST_LEAVE') == 'decision' and 'SE_FEATURE_COMBO' not in env_case and decided['Partner'] is None:
        del decided['Partner']
    problems = []
    def need(ok, what):
        if not ok: problems.append(what)
    need(all(code == 0 for code in codes.values()), 'a process failed: %s' % codes)
    need(host and partner and viewer, 'a process wrote no result')
    need(None not in decided.values() and len(set(decided.values())) == 1, 'decision cycle/digest differ: %s' % decided)
    if 'SE_FEATURE_COMBO' in env_case:
        features = [(case_out / (role + '-features')).read_text().strip()
                    if (case_out / (role + '-features')).exists() else None
                    for role in ('Host', 'Partner', 'Viewer')]
        need(None not in features and len(set(features)) == 1,
             'feature ownership/crash/kill-credit evidence differs: %s' % features)
        need(viewer.get('feature_checkpoint') == '1' and (case_out / 'Viewer-feature-checkpoint').exists(),
             'late viewer did not retain the post-crime simulation/checkpoint')
        need((case_out / 'Viewer-stalled').exists(), 'feature viewer never stalled across host departure')
        def timed_rows(name, count):
            try:
                rows = [tuple(map(int, line.split())) for line in (case_out / name).read_text().splitlines()]
                if len(rows) != count or any(len(row) != 2 for row in rows):
                    raise ValueError('wrong event shape')
                return rows
            except (OSError, ValueError) as error:
                need(False, 'invalid/missing causal stall evidence %s: %s' % (name, error))
                return None
        start = timed_rows('Viewer-stall-start', 1)
        armed = timed_rows('Host-verdict-armed', 2)
        verdict = timed_rows('Host-verdict-event', 3)
        departed = timed_rows('Host-departed', 3)
        end = timed_rows('Viewer-stall-end', 7)
        if all(event is not None for event in (start, armed, verdict, departed, end)):
            need(armed[1:] == start and verdict[1:] == armed and departed[1:] == armed,
                 'host events do not causally acknowledge this exact viewer stall start')
            need(end[1:4] == verdict and end[4:] == departed,
                 'viewer did not receive the actual host verdict AND disconnect before ending its stall')
            need(start[0][0] == end[0][0] == 1200 and start[0][0] < verdict[0][0],
                 'viewer did not stall at the scheduled pre-decision cycle')
            need(end[0][1] - start[0][1] >= int(env_case['SE_STALL_MS']) * 1_000_000,
                 'measured viewer stall shorter than requested')
            need(armed[0][0] == verdict[0][0] == int(host.get('finished_cycle', '-1'))
                 and departed[0][0] == int(host.get('last_cycle', '-1'))
                 and armed[0][1] <= verdict[0][1] <= departed[0][1],
                 'actual production verdict/disconnect cycle or host monotonic ordering differs')
        crashes = []
        for role in ('Host', 'Partner', 'Viewer'):
            log_path = case_out / (role + '.log')
            match = re.search(r'City airplane of house (\d+) shot down at (\d+),(\d+) by house (\d+) '
                              r'\(cycle (\d+)\): Death Hand impact',
                              log_path.read_text(errors='replace') if log_path.exists() else '')
            crashes.append(match.groups() if match else None)
        need(None not in crashes and len(set(crashes)) == 1,
             'real shot-down Death Hand crash logs missing or inconsistent: %s' % crashes)
        winning_role = 'Host' if args.host_wins else 'Partner'
        for role, observed in (('Host', host), ('Partner', partner), ('Viewer', viewer)):
            quit_boundary = role == 'Host' and env_case.get('SE_HOST_LEAVE') == 'decision'
            if role != 'Viewer':
                need(observed.get('won') == str(int(role == winning_role))
                     and observed.get('watched_winner') == '-1',
                     role + ': incorrect actual player verdict')
            else:
                need(observed.get('watched_winner') == ('1' if args.host_wins else '2'),
                     'Viewer: incorrect watched winner team')
            plate = expected if role == 'Viewer' else (
                'You Have Completed Your Mission.' if role == winning_role else 'You Have Failed Your Mission.')
            if quit_boundary:
                # Game::whatNext CustomMultiplayer (Game.cpp:4972-4978): bQuitGame returns
                # GAME_RETURN_TO_MENU. Do not draw a new plate or construct stats after that quit.
                need(observed.get('quit') == '1' and observed.get('stats_presented') == '0'
                     and observed.get('presentation_boundary') == 'host-decision-quit-return-to-menu',
                     'Host: immediate quit did not take the explicit production return-to-menu boundary')
            else:
                need(observed.get('finished') == '1' and observed.get('finished_level') == '1'
                     and observed.get('quit') == '0' and observed.get('stats_presented') == '1'
                     and observed.get('plate_presented') == '1',
                     role + ': normal result plate/statistics were not reached')
            if observed.get('plate_presented') == '1':
                need(observed.get('plate_text') == plate and observed.get('plate_text_white') == '1',
                     role + ': actual rendered result text/colour incorrect: ' + repr(observed.get('plate_text')))
                need(observed.get('plate_framed') == ('1' if role == 'Viewer' else '0')
                     and (role != 'Viewer' or observed.get('plate_frame_matches') == '1'),
                     role + ': actual result frame/colour incorrect')
            if not quit_boundary:
                need(observed.get('stats_text') == (expected if role == 'Viewer' else ''),
                     role + ': actual statistics banner does not match the production player/observer branch')
                need(observed.get('stats_caption_colour_matches') == '1'
                     and observed.get('stats_winner_row') == 'House ' + winner
                     and observed.get('stats_winner_colour_matches') == '1'
                     and observed.get('stats_local_colour_matches') == '1'
                     and observed.get('stats_natives_hidden') == '1',
                     role + ': actual statistics caption/rows/colours incorrect')
                if role != 'Viewer':
                    need(observed.get('stats_local_row') == ('House Atreides' if role == 'Host' else 'House Harkonnen'),
                         role + ': statistics show the wrong local player row')
    if viewer:
        need(viewer.get('join_failure', '') == '', 'viewer failed: ' + viewer.get('join_failure', ''))
        need(viewer.get('finished') == '1' and viewer.get('quit') == '0' and viewer.get('finished_level') == '1',
             'viewer did not end normally (finished=%s finished_level=%s quit=%s)' % (viewer.get('finished'), viewer.get('finished_level'), viewer.get('quit')))
        need(viewer.get('watched_text') == expected, 'viewer end plate: %r' % viewer.get('watched_text'))
        need(viewer.get('stats_text') == expected and viewer.get('stats_colour_matches') == '1',
             'viewer statistics: %r' % viewer.get('stats_text'))
        need(viewer.get('finished_cycles_run', '0') != '0', 'viewer never ran a cycle with the end plate up')
        if host:
            need(viewer.get('last_cycle') == host.get('last_cycle') and viewer.get('last_digest') == host.get('last_digest'),
                 'viewer ended at %s %s, host at %s %s' % (viewer.get('last_cycle'), viewer.get('last_digest'),
                                                           host.get('last_cycle'), host.get('last_digest')))
    if host and partner and (env_case.get('SE_HOST_LEAVE') != 'decision' or 'SE_FEATURE_COMBO' in env_case):
        need(partner.get('last_cycle') == host.get('last_cycle') and partner.get('last_digest') == host.get('last_digest'),
             'partner and host ended differently')
    line = '%s: decided %s; host last %s; viewer last %s finished=%s quit=%s failure=%r stats=%r restarts=%s' % (
        case, (decided.get('Host') or '?').split(' ')[0], host.get('last_cycle'), viewer.get('last_cycle'),
        viewer.get('finished'), viewer.get('quit'), viewer.get('join_failure'), viewer.get('stats_text'), viewer.get('restarts'))
    if 'SE_FEATURE_COMBO' in env_case:
        line += '; presentations=' + ','.join(
            role + ':' + observed.get('presentation_boundary', 'missing')
            + '/plate=' + observed.get('plate_presented', '?') + '/stats=' + observed.get('stats_presented', '?')
            for role, observed in (('Host', host), ('Partner', partner), ('Viewer', viewer)))
    print(line + ('' if not problems else '\n  ' + '\n  '.join(problems)), flush=True)
    summary.append(line + ('' if not problems else '\n  ' + '\n  '.join(problems)))
    if problems: failures.append(case)
    for context in reversed(profiles):
        context.__exit__(RuntimeError if problems else None,
                         RuntimeError('; '.join(problems)) if problems else None, None)
(out / 'summary.txt').write_text('\n'.join(summary) + '\n')
release_windows()
if failures and not args.baseline:
    print('SPECTATOR_END_PROBE_FAIL: ' + ', '.join(failures) + '; logs in ' + str(out))
    sys.exit(1)
print('SPECTATOR_END_PROBE_%s: %d cases; logs in %s' % ('BASELINE' if args.baseline else 'PASS', len(cases), out))
