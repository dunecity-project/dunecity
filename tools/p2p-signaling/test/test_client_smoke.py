#!/usr/bin/env python3
"""Compile production C++ Workshop components and exercise the real PHP service over HTTP.

Requires a configured native build/include directory, C++17, pkg-config, SDL2, curl and PHP.
This does not launch the game or read the user's profile. All server/client data is temporary.
Use --binary to test an already-built harness instead of compiling it.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from test_signaling import ServiceFixture, PHP_BIN, php_literal

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--binary', type=Path)
    args = parser.parse_args()
    if not PHP_BIN:
        raise SystemExit('PHP is required; set PHP_BIN.')
    build = args.build_dir.resolve()
    binary = args.binary.resolve() if args.binary else build / 'workshop-client-smoke'
    if not args.binary:
        if not (build / 'include').is_dir():
            raise SystemExit('Configure the native CMake build first, or pass --build-dir.')
        cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'sdl2', 'libcurl'], text=True))
        libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', 'sdl2', 'libcurl'], text=True))
        strip = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
        sources = ['tests/workshop-client-smoke.cpp', 'src/mod/WorkshopStore.cpp',
                   'src/mod/WorkshopClient.cpp', 'src/mod/Dune2RAssetManager.cpp',
                   'src/Network/BoundedHttpClient.cpp']
        command = (shlex.split(os.environ.get('CXX', 'c++'))
                   + ['-std=c++17', '-O1', '-ffunction-sections', '-fdata-sections', '-pthread',
                      '-I' + str(build / 'include'), '-I' + str(ROOT / 'include')]
                   + cflags + [strip] + [str(ROOT / p) for p in sources] + libs + ['-o', str(binary)])
        subprocess.run(command, check=True, cwd=ROOT)
    service = ServiceFixture()
    # Inject one real HTTP 429 before the first manifest request. The harness advances the
    # SDL retry clock, while the production client still decides whether/what to retry.
    flag = Path(service.tmp) / 'rate-once'
    observed = Path(service.tmp) / 'rate-observed'
    flag.write_text('1')
    router = Path(service.tmp) / 'router.php'
    injection = ("if (($_SERVER['REQUEST_URI'] ?? '') === '/v1/content/manifest' && file_exists("
                 + php_literal(str(flag)) + ")) { unlink(" + php_literal(str(flag))
                 + "); file_put_contents(" + php_literal(str(observed)) + ", '1'); "
                 + "http_response_code(429); header('Content-Type: text/plain'); "
                 + "echo \"status=error\\ncode=rate_limited\\nmessage=5265747279\\n\"; return; }\n")
    router.write_text(router.read_text().replace('<?php\n', '<?php\n' + injection, 1))
    try:
        with tempfile.TemporaryDirectory(prefix='workshop-client-wire-') as directory:
            subprocess.run([str(binary), 'http://127.0.0.1:' + str(service.port), directory], check=True, cwd=ROOT)
            if not observed.exists():
                raise RuntimeError('The client smoke test did not exercise rate-limit retry.')
    finally:
        service.stop()


if __name__ == '__main__':
    main()
