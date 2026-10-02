#!/usr/bin/env python3
"""Exercise live joining over real local PHP and WebRTC, then compare resumed game state.

Requires the existing macOS Ninja Release build and bundled game assets. Uses an isolated
profile and dummy SDL drivers. Logs and the test executable are retained in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import struct
import tempfile

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
args = parser.parse_args()
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
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-late-join-probe-'))
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runLateJoinProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
pos = main.index('int main(')
include = root / 'tests/network/late-join-probe.inc'
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

import sys, time
sys.path.insert(0,str(root/'tools/p2p-signaling/test'))
from test_signaling import ServiceFixture
service=None if args.endpoint else ServiceFixture()
if args.endpoint:
    from urllib.parse import urlsplit
    if urlsplit(args.endpoint).scheme!='https' or urlsplit(args.endpoint).hostname!='dunelegacy.com' or '/play-test-' not in urlsplit(args.endpoint).path:
        raise RuntimeError('Remote probe requires an isolated HTTPS play-test path')
processes=[]; logs=[]
if args.browser and not args.endpoint:
    import http.server, threading, json
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
    import json
    from urllib.parse import quote
    (out/'browser.json').write_text(json.dumps({'url':args.endpoint.rsplit('/p2p',1)[0]+'/?relay='+quote(args.endpoint,safe='')}))
try:
    originals=('Host',) if args.solo else ('Host','Partner')
    roles=originals if args.browser else originals+('Newcomer',)
    for role in roles:
        log=(out/(role+'.log')).open('w'); logs.append(log)
        env=dict(os.environ,DUNECITY_USERDIR=str(out/('profile-'+role)),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',JOIN_ROLE=role,JOIN_MODE=args.mode,JOIN_OUT=str(out),JOIN_ENDPOINT=args.endpoint or ('http://127.0.0.1:'+str(service.port)))
        if args.solo: env['JOIN_SOLO']='1'
        if args.city: env['JOIN_CITY']='1'
        if args.twin_cities: env['JOIN_TWIN_CITIES']='1'
        if args.browser: env['JOIN_BROWSER']='1'
        if args.busy: env['JOIN_BUSY']='1'
        if args.suspended_paths: env['JOIN_CHECKPOINT_TRACE']='1'
        if args.stall: env['JOIN_STALL']='1'
        role_binary = (args.host_binary if role=='Host' else args.newcomer_binary if role=='Newcomer' else None) or binary
        processes.append(subprocess.Popen([str(role_binary.resolve()),'--window','--showlog'],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT))
    deadline=time.monotonic()+(620 if args.browser or os.environ.get('JOIN_FAST_WARMUP') else 170)
    while time.monotonic()<deadline:
        compared=originals if args.mode=='abort' or args.browser or args.stall or os.environ.get('JOIN_DESYNC_ALWAYS') else roles
        if all((out/(role+'-digest')).exists() for role in compared) and (args.mode!='abort' or (out/'Newcomer-cancelled').exists()):
            values=[(out/(role+'-digest')).read_text() for role in compared]
            if len(set(values))!=1: raise RuntimeError('State mismatch: '+str(values))
            if args.suspended_paths:
                matched = False
                for host_runtime in out.glob('checkpoint-*-Host/runtime'):
                    viewer_runtime = host_runtime.parent.with_name(
                        host_runtime.parent.name.removesuffix('-Host') + '-Newcomer') / 'runtime'
                    if not viewer_runtime.is_file(): continue
                    payload = host_runtime.read_bytes()
                    if payload != viewer_runtime.read_bytes():
                        raise RuntimeError('Transferred path continuation bytes differ')
                    if struct.unpack_from('<I', payload)[0] != 6:
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
    else: raise RuntimeError('Join probe timed out')
    for p in processes:
        if p.wait(timeout=15)!=0: raise RuntimeError('Probe process failed')
    print(str(len(values))+' real peers continued with matching state ('+args.mode+'): '+values[0])
finally:
    for p in processes:
        if p.poll() is None: p.terminate()
    for p in processes:
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill()
    for log in logs: log.close()
    if service: service.stop()
    if args.browser and not args.endpoint: web.shutdown()
