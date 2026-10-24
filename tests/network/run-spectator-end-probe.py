#!/usr/bin/env python3
"""A decided online match, watched by a network spectator, played by separate processes.

Builds a diagnostic copy of the game whose main menu is replaced by
tests/network/spectator-end-probe.inc (production objects unchanged), starts the local signaling
service (tools/p2p-signaling, PHP) and runs Host, Partner and Viewer as separate processes that
talk over real WebRTC (ICE over the machine's interfaces). Each runs the real Game::runMainLoop.
The winner's tanks hunt the loser's troopers, so the simulation decides the match itself; the
viewer joins mid-game as a spectator. Cases:

  end            the host leaves after its six-second end message and the statistics
  decision       the host quits at the very cycle the match is decided
  lag-end        the viewer hangs 20 s from the decision: it is about 7 s behind when the host leaves
  lag-decision   the viewer hangs 25 s from the decision and the host quits at it (25 s behind)
  slow           the viewer runs at under half speed from the start (falls further behind all game)
  natives        Dune City mod with the map's sandworms (the hidden Natives house, team 0)

Every case requires: all three processes decide at the same cycle with the same state digest;
the viewer ends normally (no "connection interrupted"), on the same final cycle and digest as the
host, names the winner on its end plate and in the statistics. --baseline reports instead of failing.
--remote ROLE=HOST runs a role on another Mac over ssh (see --help of the options).
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[2]
CASES = {
    'end':          dict(),
    'decision':     dict(SE_HOST_LEAVE='decision'),
    'lag-end':      dict(SE_STALL_MS='20000'),
    'lag-decision': dict(SE_STALL_MS='25000', SE_HOST_LEAVE='decision'),
    'slow':         dict(SE_SLOW_MS='35'),
    'natives':      dict(SE_MOD='dunecity'),
}
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
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
args = parser.parse_args()
cases = [c for c in args.cases.split(',') if c]
for c in cases:
    if c not in CASES: parser.error('unknown case ' + c)
remote = dict(item.split('=', 1) for item in args.remote)
for role in remote:
    if role not in ('Host', 'Partner', 'Viewer'): parser.error('unknown role ' + role)
if not (os.environ.get('PHP_BIN') or shutil.which('php8.3') or shutil.which('php')
        or Path('/opt/homebrew/bin/php').exists()):
    print('SKIPPED: the signaling service needs PHP (brew install php)')
    sys.exit(77)
if not os.environ.get('PHP_BIN') and not shutil.which('php') and Path('/opt/homebrew/bin/php').exists():
    os.environ['PHP_BIN'] = '/opt/homebrew/bin/php'

build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-spectator-end-'))
out.mkdir(parents=True, exist_ok=True)

app = out / 'spectator-end-probe.app/Contents'
binary = args.binary.resolve() if args.binary else app / 'MacOS/spectator-end-probe'
if not args.binary:
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the test injection point.')
    main = main.replace(needle, 'int menuResult = runSpectatorEndProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    pos = main.index('int main(')
    main = main[:pos] + '#include "' + str(root / 'tests/network/spectator-end-probe.inc') + '"\n' + main[pos:]
    source, obj = out / 'spectator-end-probe-main.cpp', out / 'spectator-end-probe-main.o'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    # Test compilation must not overwrite Ninja's production dependency records.
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option); del cc[i:i + 2]
    cc = [a for a in cc if a not in ('-MD', '-MMD')]
    cc = [a for a in cc if not a.endswith('ccache')] if cc and cc[0].endswith('ccache') else cc
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc.append('-fno-access-control')
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    if not (app / 'Resources').exists():
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if a.endswith('/main.cpp.o') else a for a in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)


def probe_map(natives, host_wins):
    """Habbanya-Autumn (51x31) without bases: ten hunting tanks against three troopers."""
    text = (root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini').read_text()
    head, units = text.split('[UNITS]', 1)
    rest = units[units.index('\n['):] if '\n[' in units else ''
    # The map's Player2 is the first house of the game (the host's Atreides), Player1 the partner's.
    strong, weak = ('Player2', 'Player1') if host_wins else ('Player1', 'Player2')
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


sys.path.insert(0, str(root / 'tools/p2p-signaling/test'))
from test_signaling import ServiceFixture

SHARED = ('room', 'init', 'running', 'Host-decided')
failures = []
summary = []
for host in set(remote.values()):
    stage_remote(host)
for case in cases:
    env_case = CASES[case]
    case_out = out / case
    if case_out.exists(): shutil.rmtree(case_out)
    case_out.mkdir()
    (case_out / 'map.ini').write_text(probe_map(case == 'natives', args.host_wins))
    service = ServiceFixture()
    processes, logs, tunnels = {}, [], []
    try:
        for role in ('Host', 'Partner', 'Viewer'):
            env = {'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy', 'SE_ROLE': role,
                   'SE_ENDPOINT': 'http://127.0.0.1:%d' % service.port}
            env.update(env_case)
            if role in remote:
                host = remote[role]
                rdir = '%s/%s-%s' % (args.remote_dir, case, role)
                ssh(host, 'rm -rf %s && mkdir -p %s/profile' % (shlex.quote(rdir), shlex.quote(rdir)), check=True)
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
                env.update(SE_OUT=str(case_out), DUNECITY_USERDIR=str(case_out / ('profile-' + role)))
                log = (case_out / (role + '.log')).open('w'); logs.append(log)
                processes[role] = subprocess.Popen([str(binary), '--window', '--showlog'], cwd=case_out,
                    env=dict(os.environ, **env), stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 420
        while time.monotonic() < deadline:
            # Relay the rendezvous files between machines (local roles share case_out).
            for role, host in remote.items():
                rdir = '%s/%s-%s' % (args.remote_dir, case, role)
                listing = ssh(host, 'ls %s' % shlex.quote(rdir), capture_output=True, text=True).stdout.split()
                for name in listing:
                    if (name in SHARED or name.endswith('-result') or name.endswith('-decided')) and not (case_out / name).exists():
                        subprocess.run(['scp', '-q', host + ':' + rdir + '/' + name, str(case_out / (name + '.tmp'))], check=True)
                        (case_out / (name + '.tmp')).rename(case_out / name)
                for name in SHARED:
                    if (case_out / name).exists() and name not in listing:
                        subprocess.run(['scp', '-q', str(case_out / name), host + ':' + rdir + '/' + name + '.tmp'], check=True)
                        ssh(host, 'mv %s/%s.tmp %s/%s' % (rdir, name, rdir, name), check=True)
            if all(p.poll() is not None for p in processes.values()): break
            time.sleep(0.5 if remote else 0.2)
        else:
            raise RuntimeError(case + ': timed out; logs in ' + str(case_out))
        for role, host in remote.items():
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
        for log in logs: log.close()
        service.stop()

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
    if env_case.get('SE_HOST_LEAVE') == 'decision' and decided['Partner'] is None: del decided['Partner']
    problems = []
    def need(ok, what):
        if not ok: problems.append(what)
    need(all(code == 0 for code in codes.values()), 'a process failed: %s' % codes)
    need(host and partner and viewer, 'a process wrote no result')
    need(None not in decided.values() and len(set(decided.values())) == 1, 'decision cycle/digest differ: %s' % decided)
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
    if host and partner and env_case.get('SE_HOST_LEAVE') != 'decision':
        need(partner.get('last_cycle') == host.get('last_cycle') and partner.get('last_digest') == host.get('last_digest'),
             'partner and host ended differently')
    line = '%s: decided %s; host last %s; viewer last %s finished=%s quit=%s failure=%r stats=%r restarts=%s' % (
        case, (decided.get('Host') or '?').split(' ')[0], host.get('last_cycle'), viewer.get('last_cycle'),
        viewer.get('finished'), viewer.get('quit'), viewer.get('join_failure'), viewer.get('stats_text'), viewer.get('restarts'))
    print(line + ('' if not problems else '\n  ' + '\n  '.join(problems)), flush=True)
    summary.append(line)
    if problems: failures.append(case)
(out / 'summary.txt').write_text('\n'.join(summary) + '\n')
if failures and not args.baseline:
    print('SPECTATOR_END_PROBE_FAIL: ' + ', '.join(failures) + '; logs in ' + str(out))
    sys.exit(1)
print('SPECTATOR_END_PROBE_%s: %d cases; logs in %s' % ('BASELINE' if args.baseline else 'PASS', len(cases), out))
