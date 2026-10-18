#!/usr/bin/env python3
"""Serve the shipped browser binary with isolated profiles and an in-memory timer.

Open each printed URL sequentially, press Arm benchmark after startup, and load
benchmark-city through the game UI (Continue selects the sole fixture save).
Distinct origins prevent log files from previous runs affecting storage syncing.
The probe does not change the shipped JS/Wasm or the source save/profile.
"""
import argparse
import base64
import configparser
import hashlib
import io
import json
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root/'build-714/emscripten/bin')
parser.add_argument('--profile', type=Path, required=True)
parser.add_argument('--save', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--port', type=int, default=18731)
parser.add_argument('--seconds', type=int, default=120)
parser.add_argument('--warmup', type=int, default=10)
parser.add_argument('--runs', nargs='+', choices=('on','off'), default=['on','off','off','on'])
args = parser.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
build = args.build_dir.resolve()
save_bytes = args.save.read_bytes()
fixture_files = {}
for mod in ('dunecity','vanilla'):
    for path in (args.profile/'mods'/mod).rglob('*'):
        if path.is_file(): fixture_files[str(path.relative_to(args.profile))] = base64.b64encode(path.read_bytes()).decode()
fixture_files['mods/active_mod.txt'] = base64.b64encode(b'dunecity').decode()

def handler_for(index, mode):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.optionxform = str
    config.read(args.profile/'Dune City.ini')
    config.set('General','Diagnostic Logs','true' if mode=='on' else 'false')
    config.set('General','AI Decision Log','true' if mode=='on' else 'false')
    config.set('General','Play Intro','false')
    config.set('Video','Fullscreen','false')
    config.set('Video','Browser Display Version','1')
    ini = io.StringIO(); config.write(ini)
    files = dict(fixture_files, **{'Dune City.ini':base64.b64encode(ini.getvalue().encode()).decode()})
    fixture = {'run':index,'logging':mode,'seconds':args.seconds,'warmup':args.warmup,'files':files,
               'save_sha256':hashlib.sha256(save_bytes).hexdigest()}
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self,*a,**kw): super().__init__(*a,directory=str(build),**kw)
        def log_message(self,*a): pass
        def content(self,data,kind):
            self.send_response(200);self.send_header('Content-Type',kind)
            self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def do_GET(self):
            path=self.path.split('?')[0]
            if path=='/benchmark.js': return self.content((root/'tests/performance/browser-logging-benchmark.js').read_bytes(),'text/javascript')
            if path=='/fixture.json': return self.content(json.dumps(fixture).encode(),'application/json')
            if path=='/fixture-save': return self.content(save_bytes,'application/octet-stream')
            if path in ('/','/dunecity.html'):
                html=(build/'dunecity.html').read_text()
                marker='<script src=dunecity.js async></script>'
                if marker not in html: raise RuntimeError('Browser shell script tag changed')
                return self.content(html.replace(marker,'<script src=benchmark.js></script>'+marker).encode(),'text/html')
            return super().do_GET()
        def do_POST(self):
            if self.path!='/result': return self.send_error(404)
            result=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            if result.get('logging')!=mode: return self.send_error(400)
            (args.output_dir/f'run-{index}-{mode}.json').write_text(json.dumps(result,indent=2)+'\n')
            print(f'Completed run {index} ({mode}): {result["summary"]}',flush=True)
            return self.content(b'OK','text/plain')
    return Handler

manifest={'build':str(build),'seconds':args.seconds,'warmup':args.warmup,
          'save_sha256':hashlib.sha256(save_bytes).hexdigest(),
          'artifacts':{name:hashlib.sha256((build/name).read_bytes()).hexdigest() for name in ('dunecity.js','dunecity.wasm','dunecity.data')},'runs':[]}
servers=[]
for index,mode in enumerate(args.runs,1):
    port=args.port+index-1
    server=ThreadingHTTPServer(('127.0.0.1',port),handler_for(index,mode));servers.append(server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    url=f'http://127.0.0.1:{port}/dunecity.html'
    manifest['runs'].append({'run':index,'logging':mode,'url':url})
    print(f'Run {index}: logging {mode}: {url}',flush=True)
(args.output_dir/'fixture-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
try: threading.Event().wait()
except KeyboardInterrupt: pass
finally:
    for server in servers: server.shutdown()
