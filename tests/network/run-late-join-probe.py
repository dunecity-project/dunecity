#!/usr/bin/env python3
"""Exercise live joining over real local PHP and WebRTC, then compare resumed game state.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.

--peer-cases campaign skirmish custom --mod vanilla is the headless native join gate:
two independent processes complete admission, network settings and the start barrier,
then execute both humans' orders for 3,600 cycles with 13 matching state checkpoints.
Vanilla campaign uses the authored shared-house CampaignCoop path; skirmish really
generates a mirrored 64x64 multiplayer map (seed 20261015); custom uses Habbanya-Autumn.
This checks native loopback gameplay, not public-1.0.864/browser interoperability.
--tornie-case custom|campaign retains the original separate-faction Tornie cases.
"""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import struct
import tempfile
import sys
import time
import uuid

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--endpoint', help='Explicit isolated HTTPS test service; never use the production lobby.')
parser.add_argument('--browser',action='store_true',help='Use the browser game as Newcomer; connect using browser.json, then create browser-observed after inspection.')
parser.add_argument('--host-binary', type=Path, help='Existing probe executable for host compatibility testing (same map).')
parser.add_argument('--newcomer-binary', type=Path, help='Existing probe executable for viewer compatibility testing (same map).')
parser.add_argument('--stall',action='store_true',help='Leave the spectator unresponsive until its stream times out; the players must continue.')
parser.add_argument('--solo',action='store_true',help='One native host, with no second active peer.')
parser.add_argument('--city',action='store_true',help='Dune City, four-house Ergsun-Odenkirk, shared hard AI host.')
parser.add_argument('--four-corners',action='store_true',help='Use the four-house 128x128 map with --city.')
parser.add_argument('--twin-cities',action='store_true',help='Use the two-house 256x256 Twin Cities map with --city.')
parser.add_argument('--busy',action='store_true',help='Exercise moving armies and AI production while the observer catches up.')
parser.add_argument('--suspended-paths', action='store_true',
                    help='Require real transfer of suspended A* jobs (native solo spectator fixture only).')
parser.add_argument('--mode', choices=['replace','share_ai','share_human','abort','spectate','reject_spectate','promote'],default='replace')
peers = parser.add_mutually_exclusive_group()
peers.add_argument('--tornie-case', choices=['custom', 'campaign'],
                   help='Original Tornie two-native-peer fixture; no late join, TURN or relay.')
peers.add_argument('--peer-cases', nargs='+', choices=['campaign', 'skirmish', 'custom'],
                   help='Native real-game joins, sequentially, reusing one injected executable.')
parser.add_argument('--mod', choices=['vanilla', 'Tornie'], help='Native peer content (default vanilla; --tornie-case selects Tornie).')
args = parser.parse_args()
peer_probe = bool(args.tornie_case or args.peer_cases)
peer_cases = [args.tornie_case] if args.tornie_case else args.peer_cases
peer_mod = args.mod or ('Tornie' if args.tornie_case else 'vanilla')
peer_timeout = 600 if args.tornie_case else 240
if args.tornie_case and peer_mod != 'Tornie':
    parser.error('--tornie-case requires --mod Tornie')
if args.mod and not peer_probe:
    parser.error('--mod requires --peer-cases or --tornie-case')
if peer_cases and len(set(peer_cases)) != len(peer_cases):
    parser.error('--peer-cases must not repeat a mode')
if peer_probe and (args.browser or args.endpoint or args.solo or args.city or args.busy
                   or args.stall or args.suspended_paths or args.mode != 'replace'
                   or args.host_binary or args.newcomer_binary):
    parser.error('Native peer cases are standalone two-native-peer fixtures')
if args.suspended_paths and (not args.solo or args.browser or args.mode != 'spectate'):
    parser.error('--suspended-paths requires a native --solo --mode spectate fixture')
if args.twin_cities and not args.city:
    parser.error('--twin-cities requires --city')
if args.four_corners and not args.city:
    parser.error('--four-corners requires --city')
if args.four_corners and args.twin_cities:
    parser.error('Choose one map: --four-corners or --twin-cities')
if os.environ.get('JOIN_FAST_WARMUP') and (not args.solo or args.mode not in ('spectate', 'reject_spectate')):
    parser.error('JOIN_FAST_WARMUP requires --solo and a spectator-only mode')
if os.environ.get('JOIN_MATCH_CONTROLS') and (args.solo or args.browser or args.mode!='spectate'):
    parser.error('JOIN_MATCH_CONTROLS requires two native players and --mode spectate')
if peer_probe:
    if not (os.environ.get('PHP_BIN') or shutil.which('php8.3') or shutil.which('php')
            or Path('/opt/homebrew/bin/php').is_file()):
        print('SKIPPED: native join acceptance needs PHP signaling; no modes were exercised')
        sys.exit(77)
    if not os.environ.get('PHP_BIN') and not shutil.which('php') and Path('/opt/homebrew/bin/php').is_file():
        os.environ['PHP_BIN'] = '/opt/homebrew/bin/php'


def terminate_probe(signum, _frame):
    # CTest cancellation must unwind child processes and their scratch leases.
    raise SystemExit(128 + signum)


signal.signal(signal.SIGTERM, terminate_probe)
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-late-join-probe-'))
out.mkdir(parents=True, exist_ok=True)
if peer_probe and (any((out / case).exists() for case in peer_cases) or (out / 'room').exists()):
    # Never reuse a stale room/done/digest or replace previous acceptance evidence.
    out = out / ('run-' + uuid.uuid4().hex[:10])
    out.mkdir()
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = ' + ('runNativePeerProbe' if peer_probe else 'runLateJoinProbe') + '();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
if peer_probe:
    clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
    if main.count(clamp) != 1:
        raise RuntimeError('Window-size fixture injection point changed.')
    main = main.replace(clamp, '(void)displayIndex;')
pos = main.index('int main(')
include = root / 'tests/network' / ('tornie-peer-probe.inc' if peer_probe else 'late-join-probe.inc')
main = main[:pos] + '#include "' + str(include) + '"\n' + main[pos:]
source = out / 'late-join-probe-main.cpp'
source.write_text(main)
cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i+2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'late-join-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
if args.city: map_path = root / 'data/maps/multiplayer/4P - 128x128 - Ergsun-Odenkirk.ini'
if args.four_corners: map_path = root / 'data/maps/singleplayer/4P - 128x128 - 4 corners.ini'
if args.twin_cities: map_path = root / 'data/maps/singleplayer/2P - 256x256 - Twin Cities.ini'
cc.append('-fno-access-control')
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
app = out / 'late-join-probe.app/Contents'
(app / 'MacOS').mkdir(parents=True, exist_ok=True)
resources = app / 'Resources'
if not resources.exists():
    resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/late-join-probe'
link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
game_cc = None
if args.suspended_paths:
    # Private probe object only: create a suspension immediately before the
    # snapshot is captured. The solo host has no active player peer to alter.
    # Keep every existing frontier; only seed requests if none is active. The
    # complete modified world and schedule are both transferred to the viewer.
    game_source = (root / 'src/Game.cpp').read_text()
    marker = '        const auto snapshot=spectatorSnapshot(); const auto runtime=saveObserverRuntime();'
    if game_source.count(marker) != 1:
        raise RuntimeError('Observer snapshot capture point changed')
    hook = '''        if(activePathJobs.empty()) {
            size_t seeded = 0;
            for(auto* unit : unitList) {
                if(unit->isAFlyingUnit() || !unit->isActive()
                   || !currentGameMap->tileExists(unit->getLocation())) continue;
                const Coord goal(currentGameMap->getSizeX()-1-unit->getLocation().x,
                                 currentGameMap->getSizeY()-1-unit->getLocation().y);
                unit->doMove2Pos(goal, true);
                queuePathRequest(unit->getObjectID());
                if(++seeded == 4) break;
            }
            const size_t savedBudget = negotiatedBudget, savedCarry = carryOverTokens;
            negotiatedBudget = 1; carryOverTokens = 0;
            processPathRequests();
            negotiatedBudget = savedBudget; carryOverTokens = savedCarry;
        }
        if(activePathJobs.empty()) throw std::runtime_error("No suspended path jobs at observer capture");
        SDL_Log("JOIN_PENDING_PATH_CHECKPOINT: cycle=%u jobs=%zu", gameCycleCount, activePathJobs.size());
'''
    game_source = game_source.replace(marker, hook + marker)
    game_path, game_obj = out / 'late-join-probe-game.cpp', out / 'late-join-probe-game.o'
    game_path.write_text(game_source)
    game_cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/Game.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in game_cc:
            i = game_cc.index(option); del game_cc[i:i+2]
    game_cc = [arg for arg in game_cc if arg not in ('-MD', '-MMD')]
    game_cc[game_cc.index('-o')+1] = str(game_obj)
    game_cc[game_cc.index('-c')+1] = str(game_path)
    game_cc.append('-fno-access-control')
    link = [str(game_obj) if arg.endswith('/Game.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    if game_cc:
        subprocess.run(game_cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

sys.path.insert(0,str(root/'tools/p2p-signaling/test'))
from test_signaling import ServiceFixture
def run_probe(out, peer_case=None):
    """Run one existing fixture; every peer case gets fresh profiles/service/evidence."""
    out.mkdir(parents=True, exist_ok=True)
    originals = ('Host',) if args.solo else ('Host', 'Partner')
    roles = originals if args.browser or peer_probe else originals + ('Newcomer',)
    processes, logs = [], []
    service = web = None
    previous_tempdir = tempfile.tempdir
    with ExitStack() as lifecycle:
        profile_root = out
        try:
            if peer_probe:
                sys.path.insert(0, str(root / 'tests/performance'))
                from probe_profile import MINIMAL_INI, managed_profile, require_free_space, scratch_dir
                require_free_space()
                profile_root = lifecycle.enter_context(scratch_dir('native-network-' + peer_case))
                tempfile.tempdir = str(profile_root)
            service = None if args.endpoint else ServiceFixture()
            if peer_probe:
                service.write_config(ice_servers=[])
                (out / 'transport.json').write_text(json.dumps({
                    'case': peer_case, 'mod': peer_mod,
                    'transport': 'DirectP2P', 'backend': 'libdatachannel/P2PKit',
                    'signaling': 'http://127.0.0.1:' + str(service.port),
                    'ice_servers': [], 'turn': False, 'gameplay_relay': False,
                }, indent=2) + '\n')
            if args.endpoint:
                from urllib.parse import urlsplit
                if urlsplit(args.endpoint).scheme!='https' or urlsplit(args.endpoint).hostname!='dunelegacy.com' or '/play-test-' not in urlsplit(args.endpoint).path:
                    raise RuntimeError('Remote probe requires an isolated HTTPS play-test path')
            if args.browser and not args.endpoint:
                import http.server, threading
                class BrowserFiles(http.server.SimpleHTTPRequestHandler):
                    def __init__(self,*a,**kw): super().__init__(*a,directory=str(build/'emscripten/bin'),**kw)
                    def do_POST(self):
                        # Match production's same-origin service routing; keep the browser CSP intact.
                        import http.client
                        conn=http.client.HTTPConnection('127.0.0.1',service.port,timeout=30)
                        body=self.rfile.read(int(self.headers.get('Content-Length','0')))
                        headers={k:v for k,v in self.headers.items() if k.lower() in ('content-type','origin','x-dune-session')}
                        conn.request('POST',self.path,body=body,headers=headers)
                        response=conn.getresponse(); data=response.read()
                        self.send_response(response.status)
                        for k,v in response.getheaders():
                            if k.lower() not in ('connection','transfer-encoding','server','date'):
                                self.send_header(k,v)
                        self.end_headers(); self.wfile.write(data); conn.close()
                    def log_message(self,*a): pass
                    def end_headers(self):
                        self.send_header('Cross-Origin-Opener-Policy','same-origin')
                        self.send_header('Cross-Origin-Embedder-Policy','require-corp')
                        super().end_headers()
                web=http.server.ThreadingHTTPServer(('127.0.0.1',0),BrowserFiles)
                origin='http://127.0.0.1:'+str(web.server_port)
                service.write_config(allowed_origins=[origin])
                threading.Thread(target=web.serve_forever,daemon=True).start()
                (out/'browser.json').write_text(json.dumps({'url':origin+'/dunecity.html?relay='+origin+'&relaydev=1'}))
            if args.browser and args.endpoint:
                from urllib.parse import quote
                (out/'browser.json').write_text(json.dumps({'url':args.endpoint.rsplit('/p2p',1)[0]+'/?relay='+quote(args.endpoint,safe='')}))
            for role in roles:
                log=(out/(role+'.log')).open('w'); logs.append(log)
                profile = profile_root / ('profile-' + role)
                if peer_probe:
                    profile.mkdir()
                    lifecycle.enter_context(managed_profile(profile))
                    (profile / 'Dune City.ini').write_text(MINIMAL_INI)
                env=dict(os.environ,DUNECITY_USERDIR=str(profile),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',JOIN_ROLE=role,JOIN_MODE=args.mode,JOIN_OUT=str(out),JOIN_ENDPOINT=args.endpoint or ('http://127.0.0.1:'+str(service.port)))
                if peer_probe:
                    env.update(JOIN_PEER_CASE=peer_case, JOIN_PEER_MOD=peer_mod,
                               JOIN_PEER_TIMEOUT_MS=str(peer_timeout * 1000), SDL_RENDER_DRIVER='software')
                if args.solo: env['JOIN_SOLO']='1'
                if args.city: env['JOIN_CITY']='1'
                if args.twin_cities: env['JOIN_TWIN_CITIES']='1'
                if args.browser: env['JOIN_BROWSER']='1'
                if args.busy: env['JOIN_BUSY']='1'
                if args.suspended_paths: env['JOIN_CHECKPOINT_TRACE']='1'
                if args.stall: env['JOIN_STALL']='1'
                role_binary = (args.host_binary if role=='Host' else args.newcomer_binary if role=='Newcomer' else None) or binary
                if peer_probe:
                    (out / (role + '-launch.json')).write_text(json.dumps({
                        'command': [str(role_binary.resolve()), '--window', '--showlog'],
                        'cwd': str(out), 'profile': str(profile), 'case': peer_case, 'mod': peer_mod,
                        'role': role, 'endpoint': env['JOIN_ENDPOINT'],
                        'video_driver': env['SDL_VIDEODRIVER'], 'render_driver': env['SDL_RENDER_DRIVER'],
                    }, indent=2) + '\n')
                processes.append(subprocess.Popen([str(role_binary.resolve()),'--window','--showlog'],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT))
            deadline=time.monotonic()+(620 if args.browser or os.environ.get('JOIN_FAST_WARMUP') else 170)
            if peer_probe: deadline = time.monotonic() + peer_timeout + 20
            while time.monotonic()<deadline:
                compared=originals if peer_probe or args.mode=='abort' or args.browser or args.stall or os.environ.get('JOIN_DESYNC_ALWAYS') else roles
                if all((out/(role+'-digest')).exists() for role in compared) and (args.mode!='abort' or (out/'Newcomer-cancelled').exists()):
                    values=[(out/(role+'-digest')).read_text() for role in compared]
                    if len(set(values))!=1: raise RuntimeError('State mismatch: '+str(values))
                    if peer_probe:
                        for suffix in ('settings', 'map.ini', 'trace', 'commands'):
                            evidence = [(out / (role + '-' + suffix)).read_bytes() for role in originals]
                            if len(set(evidence)) != 1:
                                raise RuntimeError(peer_case + ' native peer ' + suffix + ' mismatch')
                        trace = (out / 'Host-trace').read_text().splitlines()
                        if len(trace) != 13:
                            raise RuntimeError('Expected 13 matching checkpoints, cycles 0 through 3600')
                        if peer_case == 'skirmish' and (out / 'generated-map.ini').read_bytes() != (out / 'Host-map.ini').read_bytes():
                            raise RuntimeError('The played skirmish map is not the real generator output')
                        summary = {
                            'case': peer_case, 'mod': peer_mod, 'status': 'PASS', 'transport': 'DirectP2P',
                            'peers': list(originals), 'pids': [p.pid for p in processes],
                            'actual_guest': 'Partner', 'start_barrier': 'committed',
                            'final_cycle': 3600, 'matching_checkpoints': 13,
                            'matching_settings': True, 'matching_map': True, 'matching_executed_commands': True,
                            'executed_moves_per_player': 12, 'both_units_moved': True, 'final_digest': values[0],
                            'generated_skirmish': peer_case == 'skirmish',
                            'generator_seed': 20261015 if peer_case == 'skirmish' else None,
                        }
                    if args.suspended_paths:
                        matched = False
                        for host_runtime in out.glob('checkpoint-*-Host/runtime'):
                            viewer_runtime = host_runtime.parent.with_name(
                                host_runtime.parent.name.removesuffix('-Host') + '-Newcomer') / 'runtime'
                            if not viewer_runtime.is_file(): continue
                            payload = host_runtime.read_bytes()
                            if payload != viewer_runtime.read_bytes():
                                raise RuntimeError('Transferred path continuation bytes differ')
                            # Runtime8 appends bot authority/snapshots; the queue/path prefix remains unchanged.
                            if struct.unpack_from('<I', payload)[0] != 8:
                                raise RuntimeError('Update the suspended-path fixture for the observer runtime version')
                            offset = 24
                            targets = struct.unpack_from('<I', payload, offset)[0]
                            offset += 4 + targets * 4
                            paths = struct.unpack_from('<I', payload, offset)[0]
                            offset += 4 + paths * 4
                            suspended = struct.unpack_from('<I', payload, offset)[0]
                            if not 1 <= suspended <= 4:
                                raise RuntimeError('The real checkpoint did not contain bounded suspended searches')
                            matched = True
                        if not matched:
                            raise RuntimeError('No matching host/viewer suspended-search checkpoint was captured')
                    if args.browser and not (out/'browser-observed').exists(): time.sleep(.2); continue
                    if args.mode in ('spectate','reject_spectate') and not args.browser:
                        after=[out/(role+'-after-leave') for role in originals]
                        if not all(p.exists() for p in after): time.sleep(.2); continue
                        if len({p.read_text() for p in after})!=1: raise RuntimeError('State diverged after spectator left')
                    (out/'done').write_text('yes'); break
                if any(p.poll() is not None and not (roles[i]=='Newcomer' and ((args.mode=='abort' and (out/'Newcomer-cancelled').exists()) or (args.mode in ('spectate','reject_spectate') and (out/'Newcomer-left').exists()))) for i,p in enumerate(processes)): raise RuntimeError('Probe process ended early; inspect logs in '+str(out))
                time.sleep(.2)
            else: raise RuntimeError('Join probe timed out: ' + str(out))
            for p in processes:
                if p.wait(timeout=15)!=0: raise RuntimeError('Probe process failed: ' + str(out))
            if peer_probe:
                for role in originals:
                    text = (out / (role + '.log')).read_text(errors='replace')
                    for marker in ('NATIVE_PEER_ADMITTED', 'NATIVE_PEER_STARTED', 'NATIVE_PEER_PROBE_PASS'):
                        expected = f'{marker}: mode={peer_case} mod={peer_mod} role={role}'
                        if text.count(expected) != 1:
                            raise RuntimeError('Missing actual mode/admission/start/gameplay marker: ' + expected)
                    observed = dict(line.split('=', 1) for line in (out / (role + '-details')).read_text().splitlines() if '=' in line)
                    expected = {'case': peer_case, 'mod': peer_mod, 'role': role, 'guest_joined': 'Partner',
                                'remote': 'Partner' if role == 'Host' else 'Host', 'start_barrier': 'committed',
                                'generated': str(int(peer_case == 'skirmish')),
                                'ownership': 'shared-house' if peer_case == 'campaign' and peer_mod == 'vanilla' else 'separate-houses'}
                    if any(observed.get(key) != value for key, value in expected.items()):
                        raise RuntimeError('Actual native mode/guest evidence does not match: ' + str(observed))
                (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
                print(f'NATIVE_JOIN_MODE_PASS: mode={peer_case} mod={peer_mod} guest=Partner '
                      f'cycles=3600 moves=12/12 checkpoints=13; evidence={out}', flush=True)
                return summary
            print(str(len(values))+' real peers continued with matching state ('+args.mode+'): '+values[0])
        finally:
            for p in processes:
                if p.poll() is None: p.terminate()
            for p in processes:
                try: p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait(timeout=5)
            for log in logs: log.close()
            if peer_probe:
                for role in originals:
                    profile = profile_root / ('profile-' + role)
                    evidence = out / ('profile-' + role)
                    evidence.mkdir(exist_ok=True)
                    for name in ('Dune City.ini', 'Dune City.log'):
                        if (profile / name).is_file():
                            shutil.copy2(profile / name, evidence / name)
                if service:
                    shutil.copy2(Path(service.tmp) / 'php-server.log', out / 'php-server.log')
            if service: service.stop()
            if web: web.shutdown()
            tempfile.tempdir = previous_tempdir


if peer_probe:
    results = [run_probe(out if args.tornie_case else out / case, case) for case in peer_cases]
    (out / 'native-join-summary.json').write_text(json.dumps({
        'status': 'PASS', 'mod': peer_mod, 'requested_modes': peer_cases, 'cases': results,
    }, indent=2) + '\n')
    print(f'NATIVE_JOIN_ACCEPTANCE_PASS: mod={peer_mod} modes={",".join(peer_cases)}; evidence={out}', flush=True)
else:
    run_probe(out)
