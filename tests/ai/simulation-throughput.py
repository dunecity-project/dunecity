#!/usr/bin/env python3
"""Cluster-distributable, fingerprinted headless CPU/RSS benchmarks.

Fixed cases cover the real formation, six-player city/vanilla, campaign-partner
and All against Atreides workloads. Submit with dunecity-queue --machines to
measure named hosts; --rounds supplies enough independent games to fill slots.
Profilers are opt-in and should not be enabled for throughput measurements.
"""
import argparse
import csv
import hashlib
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir
from simulation_measure import measure_runner

CASES = ('duel128', 'duel160', 'duel192', 'city160', 'vanilla160', 'campaign', 'aaa1', 'aaa2')
FIELDS = ('id', 'case', 'round', 'host', 'platform', 'cpu_count', 'started_epoch', 'ended_epoch', 'wall_s', 'cpu_s',
          'probe_wall_s', 'probe_cpu_s', 'runner_cpu_s', 'parse_cpu_s', 'peak_rss_mib', 'steady_rss_mib',
          'startup_wall_s', 'setup_ms', 'loop_ms', 'cycles', 'startup', 'headless_assets', 'signature', 'bench', 'error')


def read(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, row):
    path = Path(path)
    new = not path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if new:
            writer.writeheader()
        writer.writerow(row)


def schedule(args):
    chosen = args.cases.split(',')
    unknown = set(chosen) - set(CASES)
    if unknown:
        raise ValueError('Unknown cases: ' + ','.join(sorted(unknown)))
    return [dict(id=f'{case}/{index}', case=case, round=index)
            for index in range(args.rounds) for case in chosen]


def case_arguments(case):
    if case.startswith('duel'):
        size = case[4:]
        seed, sides = {'128': ('7101', 'atreides:none,harkonnen:none'),
                       '160': ('7202', 'ordos:none,atreides:none'),
                       '192': ('7303', 'harkonnen:none,ordos:none')}[size]
        return ['--duel-map-size', size, '--duel-map-seed', seed, '--duel-sides', sides,
                '--local-spectator', '--enemy-difficulty', 'hard', '--partner-difficulty', 'hard',
                '--minutes', '20', '--seed', seed], dict(size=size, seed=seed, sides=sides, minutes=20)
    if case in ('city160', 'vanilla160'):
        city = case == 'city160'
        houses = ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary')
        sides = ','.join(f'{house}:{index + 1}:qBotHard' for index, house in enumerate(houses))
        # Audit matrix settings, with six independent Hard houses and a 160 map.
        spec = ('size=160,base=medium,army=2,city=0,spice=2,sand=65,rock=30,mountain=5,'
                'tech=9,credits=3000,seed=9101')
        return ['--scenario', '--mod', 'dunecity' if city else 'vanilla', '--house', 'harkonnen',
                '--scenario-sides', sides, '--local-spectator', '--skirmish', spec,
                '--seed', '9101', '--minutes', '120' if city else '90'], {}
    if case == 'campaign':
        # The real human-partner gate, not its AI-only diagnostic (Atreides L7/m17).
        return ['--scenario', '--mod', 'vanilla', '--house', 'atreides', '--level', '7', '--mission', '17',
                '--partner-difficulty', 'brutal', '--enemy-ai', 'campaign', '--seed', '80762', '--minutes', '80'], {}
    seed = '1756374161' if case == 'aaa1' else '906213928'
    sides = ('harkonnen:1:AIPlayerHard,atreides:2:qBotBrutal,ordos:1:AIPlayerHard,'
             'fremen:1:AIPlayerHard,sardaukar:1:AIPlayerHard')
    return ['--scenario', '--mod', 'vanilla', '--house', 'harkonnen', '--scenario-sides', sides,
            '--local-spectator', '--custom-map', str(root / 'data/maps/singleplayer/5P - 128x128 - All against Atreides.ini'),
            '--game-options', str(root / 'tests/ai/all-against-atreides-options.ini'),
            '--seed', seed, '--minutes', '51'], {}


def build_probe(args, scratch):
    out = Path(scratch) / 'probe'
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
                    '--build-dir', str(args.build_dir), '--output-dir', str(out), '--build-only'],
                   cwd=root, check=True, capture_output=True, text=True)
    return probe_binary(out)


def fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def play(args, scratch, binary, game):
    started, epoch_started = time.monotonic(), time.time()
    name = 'g' + hashlib.sha256(game['id'].encode()).hexdigest()[:12]
    out = Path(scratch) / name
    results = Path(args.results)
    # The cluster archives scratch evidence, not arbitrary files in results/.
    # Do not create out yet: the ordinary runner requires it not to exist.
    evidence = out / 'diagnostics'
    console = Path(scratch) / (name + '.runner.log')
    runner_profile = Path(scratch) / (name + '.runner.pstats')
    measurement = evidence / 'native.json'
    env = dict(os.environ, DUNECITY_SIM_MEASURE=str(measurement),
               DUNECITY_SIM_SAMPLE_SECONDS=str(args.sample_seconds), DUNECITY_ASSET_TIMING='1')
    if args.headless_assets:
        env['DUNECITY_HEADLESS_ASSETS'] = '1'
    if args.heap_profile:
        env['BALANCE_HEAP_PROFILE'] = '1'
    if args.heap_tools:
        env['BALANCE_HEAP_TOOLS'] = '1'
    if args.malloc_stacks and sys.platform == 'darwin':
        env['DUNECITY_SIM_MALLOC_STACKS'] = 'lite'
    case, bench = case_arguments(game['case'])
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--capture-mib', '256',
           '--path-workers', '1', '--no-ai-telemetry', '--wall-timeout', str(args.wall_timeout)] + case
    if args.ai_telemetry:
        cmd.remove('--no-ai-telemetry')
    if args.runner_profile:
        cmd = [sys.executable, '-m', 'cProfile', '-o', str(runner_profile)] + cmd[1:]
    env['BALANCE_DUNECITY_AUDIT'] = '1'
    rc, cpu, _ = measure_runner(cmd, console, env=env, cwd=root)
    evidence.mkdir(parents=True, exist_ok=True)
    shutil.copy2(console, evidence / 'runner.log')
    console.unlink()
    if args.runner_profile and runner_profile.exists():
        import pstats
        with (evidence / 'runner-profile.txt').open('w') as stream:
            pstats.Stats(str(runner_profile), stream=stream).strip_dirs().sort_stats('cumulative').print_stats(100)
        runner_profile.unlink()
    parse_started = time.process_time()
    lines = (out / 'run.log').read_text(errors='replace').splitlines() if (out / 'run.log').exists() else []
    native = json.loads(measurement.read_text()) if measurement.exists() else {}
    timing = next((fields(line, 'PROBE_TIMING:') for line in lines if 'PROBE_TIMING:' in line), {})
    digests = [fields(line, 'PROBE_DIGEST:') for line in lines if 'PROBE_DIGEST:' in line]
    markers = ('SCENARIO_RESULT:', 'SCENARIO_HOUSE:', 'FORMATION_DUEL_RESULT:', 'FORMATION_DUEL_HOUSE:',
               'FORMATION_DUEL_MAP:', 'FORMATION_WAVE', 'FORMATION_HOUSE:', 'CAMPAIGN_BALANCE_RESULT:')
    signature = {'digests': digests, 'results': [line.split('INFO: ', 1)[-1].strip() for line in lines
                                              if any(marker in line for marker in markers)]}
    error = '' if rc == 0 and native and timing and digests and digests[-1].get('final') == '1' else f'rc={rc};incomplete'
    if args.sample_seconds and sys.platform == 'darwin' and not (evidence / 'native.sample.txt').is_file():
        error = ';'.join(filter(None, (error, 'sample_missing')))
    startup = [line.split('ASSET_STARTUP:', 1)[1].strip() for line in lines if 'ASSET_STARTUP:' in line]
    row = dict(id=game['id'], case=game['case'], round=game['round'], host=platform.node(), platform=sys.platform,
               started_epoch=epoch_started, ended_epoch=time.time(), startup_wall_s=native.get('startup_wall_s', ''),
               cpu_count=os.cpu_count(), wall_s=round(time.monotonic() - started, 6), cpu_s=cpu,
               probe_wall_s=native.get('wall_s', ''), probe_cpu_s=native.get('cpu_s', ''),
               runner_cpu_s=round(cpu - native.get('cpu_s', 0), 6),
               parse_cpu_s=round(time.process_time() - parse_started, 6),
               peak_rss_mib=native.get('peak_rss_mib', ''), steady_rss_mib=native.get('steady_rss_mib', ''),
               setup_ms=timing.get('setup_ms', ''), loop_ms=timing.get('loop_ms', ''), cycles=timing.get('cycles', ''),
               startup=json.dumps(startup), headless_assets=int(args.headless_assets),
               signature=json.dumps(signature, separators=(',', ':')), bench='', error=error)
    if bench:
        result = next((fields(line, 'FORMATION_DUEL_RESULT:') for line in lines if 'FORMATION_DUEL_RESULT:' in line), {})
        bench.update(label=args.label, jobs=args.jobs, rc=rc, cpu_s=cpu, wall_s=row['wall_s'],
                     max_rss_mib=row['peak_rss_mib'], setup_ms=row['setup_ms'], loop_ms=row['loop_ms'],
                     cycles=row['cycles'], cycles_per_s=timing.get('cycles_per_s', 0),
                     digests=[f'{entry["cycle"]}:{entry["digest"]}' for entry in digests],
                     winner=result.get('winner', ''), end_s=result.get('end_s', ''),
                     houses=[line.split('FORMATION_DUEL_HOUSE:', 1)[1].strip() for line in lines if 'FORMATION_DUEL_HOUSE:' in line],
                     measurements=[line.strip() for line in lines if 'FORMATION_WAVE' in line or 'FORMATION_HOUSE:' in line
                                   or 'FORMATION_DUEL_MAP:' in line])
        row['bench'] = json.dumps(bench, separators=(',', ':'))
    append(results / 'games.csv', row)
    if not os.environ.get('CLUSTER_SCRATCH'):
        retained = results / 'diagnostics' / name
        shutil.copytree(evidence, retained, dirs_exist_ok=True)
        for source in out.iterdir():
            if source.is_file() and source.suffix in ('.log', '.json', '.txt'):
                shutil.copy2(source, retained / source.name)
    finish_game(scratch, out, bool(error))
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    print('| host | case | games | CPU s/game | wall s/game | native peak MiB | native steady MiB | runner CPU s |')
    print('|---|---|---:|---:|---:|---:|---:|---:|')
    for host, case in sorted({(row['host'], row['case']) for row in rows}):
        group = [row for row in rows if row['host'] == host and row['case'] == case]
        mean = lambda key: statistics.mean([float(row[key]) for row in group if row[key] not in ('', 'None')] or [float('nan')])
        print(f'| {host} | {case} | {len(group)} | {mean("cpu_s"):.3f} | {mean("wall_s"):.3f} | '
              f'{mean("peak_rss_mib"):.1f} | {mean("steady_rss_mib"):.1f} | {mean("runner_cpu_s"):.3f} |')
    expected = {game['id'] for game in schedule(args)}
    return bool(any(row['error'] for row in rows) or expected != {row['id'] for row in rows})


def compare(first, second):
    def load(folder):
        result = {}
        for row in read(Path(folder) / 'games.csv'):
            if row['error']:
                raise ValueError('Failed game: ' + row['id'])
            result.setdefault(row['case'], []).append(json.loads(row['signature']))
        if not result:
            raise ValueError('No games in ' + str(folder))
        return result
    a, b = load(first), load(second)
    same = a.keys() == b.keys()
    for case in sorted(a.keys() & b.keys()):
        signatures = {json.dumps(value, sort_keys=True) for value in a[case] + b[case]}
        identical = len(signatures) == 1
        same &= identical
        print(f'{case}: {len(a[case])}+{len(b[case])} runs, {len(a[case][0]["digests"])} digests: '
              + ('identical' if identical else 'DIFFERENT'))
    if a.keys() != b.keys():
        print('Missing cases:', sorted(a.keys() ^ b.keys()))
    return 0 if same else 1


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {row['id'] for row in read(results / 'games.csv') if not row['error']}
    require_free_space()
    with scratch_dir('simulation-throughput', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(lambda game: play(args, scratch, binary, game),
                          [game for game in schedule(args) if game['id'] not in done]))
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', required=False)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--scratch')
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--rounds', type=int, default=1)
parser.add_argument('--cases', default=','.join(CASES))
parser.add_argument('--label', default='simulation')
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--sample-seconds', type=float, default=0)
parser.add_argument('--heap-profile', action='store_true')
parser.add_argument('--heap-tools', action='store_true')
parser.add_argument('--malloc-stacks', action='store_true')
parser.add_argument('--headless-assets', action='store_true')
parser.add_argument('--ai-telemetry', action='store_true')
parser.add_argument('--runner-profile', action='store_true', help='Retain a cProfile table for the Python launcher')
parser.add_argument('--report', action='store_true')
parser.add_argument('--compare', nargs=2)
parser.add_argument('--bench-export', type=Path)
args = parser.parse_args()
if args.compare:
    sys.exit(compare(*args.compare))
if not args.results:
    parser.error('--results is required')
if args.bench_export:
    args.bench_export.write_text(''.join(row['bench'] + '\n' for row in read(Path(args.results) / 'games.csv') if row['bench']))
elif args.report:
    sys.exit(report(args))
else:
    sys.exit(run(args))
