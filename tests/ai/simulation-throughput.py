#!/usr/bin/env python3
"""Cluster-distributable, fingerprinted headless CPU/RSS benchmarks.

Fixed cases cover the real formation, six-player city/vanilla, campaign-partner
and All against Atreides workloads. Submit with dunecity-queue --machines to
measure named hosts; --rounds supplies enough independent games to fill slots.
Profilers are opt-in and should not be enabled for throughput measurements.

stress256 is the unit/combat CPU stress (docs/game-performance.md "Stress scenario"):
six QuantBot Brutal free-for-all on the 256x256 six-player map with no unit cap,
one game per --stress-seeds seed, measured cycle by cycle (game-perf.inc: simulation
and QuantBot thread CPU per tick, per-subsystem totals, peak RSS), every QuantBot on
--style if given. --perf-gate NATIVE CANDIDATE prints the promotion check's one line.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
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


def _game_perf():
    spec = importlib.util.spec_from_file_location('game_perf', root / 'tests/performance/game-perf.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GAME_PERF = _game_perf()
DEFAULT_CASES = ('duel128', 'duel160', 'duel192', 'city160', 'vanilla160', 'campaign', 'aaa1', 'aaa2')
STRESS = 'stress256'
CASES = DEFAULT_CASES + ('vanilla160-long', STRESS)
STRESS_MAP = root / 'data/maps/multiplayer/6P - 256x256 - Alkozeltser 4.ini'
STRESS_OPTIONS = root / 'tests/ai/game-options/stress-no-unit-cap.ini'
STRESS_SEEDS = (1101, 2202, 3303, 4404, 5505, 6606)
# Unit/combat promotion check (docs/ai-baseline-gate.md): the candidate's p95 tick costs may
# grow at most this much over native's on the same seeds and source.
PERF_GATE_RATIO = 1.10
FIELDS = ('id', 'case', 'round', 'host', 'platform', 'cpu_count', 'started_epoch', 'ended_epoch', 'wall_s', 'cpu_s',
          'probe_wall_s', 'probe_cpu_s', 'runner_cpu_s', 'parse_cpu_s', 'peak_rss_mib', 'steady_rss_mib',
          'startup_wall_s', 'setup_ms', 'loop_ms', 'cycles', 'startup', 'headless_assets', 'signature', 'bench',
          'source', 'stress', 'perf', 'profile', 'error')


def read(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, row):
    path = Path(path)
    new = not path.exists()
    if not new:
        with path.open(newline='') as handle:
            header = next(csv.reader(handle), [])
        if tuple(header) != FIELDS:
            raise ValueError(f'{path} has another column layout; use a new --results directory')
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
    games = []
    for index in range(args.rounds):
        for case in chosen:
            if case == STRESS:
                games += [dict(id=f'{case}-{seed}/{index}', case=case, round=index, seed=seed)
                          for seed in args.stress_seeds]
            else:
                games.append(dict(id=f'{case}/{index}', case=case, round=index))
    return games


def case_arguments(case):
    if case.startswith('duel'):
        size = case[4:]
        seed, sides = {'128': ('7101', 'atreides:none,harkonnen:none'),
                       '160': ('7202', 'ordos:none,atreides:none'),
                       '192': ('7303', 'harkonnen:none,ordos:none')}[size]
        return ['--duel-map-size', size, '--duel-map-seed', seed, '--duel-sides', sides,
                '--local-spectator', '--enemy-difficulty', 'hard', '--partner-difficulty', 'hard',
                '--minutes', '20', '--seed', seed], dict(size=size, seed=seed, sides=sides, minutes=20)
    if case in ('city160', 'vanilla160', 'vanilla160-long'):
        city, long_game = case == 'city160', case == 'vanilla160-long'
        houses = ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary')
        controller = 'qBotEasy' if long_game else 'qBotHard'
        sides = ','.join(f'{house}:{index + 1}:{controller}' for index, house in enumerate(houses))
        # Keep the fixed audit-style cases; the extra long fixture delays first
        # contact with a yard-only opening, not by forcing play beyond victory.
        start = 'base=cy,army=0,city=0,spice=3' if long_game else 'base=medium,army=2,city=0,spice=2'
        spec = f'size=160,{start},sand=65,rock=30,mountain=5,tech=9,credits=3000,seed=9101'
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


def stress_arguments(args, seed):
    """All six seats QuantBot Brutal, each its own team, the local spectator watching; no unit cap."""
    case = ['--custom-map', str(STRESS_MAP), '--house', 'atreides', '--local-spectator', '--free-for-all',
            '--mod', args.stress_mod, '--partner-difficulty', 'brutal', '--enemy-difficulty', 'brutal',
            '--game-options', str(STRESS_OPTIONS), '--seed', str(seed), '--minutes', str(args.stress_minutes + 1)]
    return case + (['--scenario-style', args.style] if args.style else [])


def build_probe(args, scratch):
    out = Path(scratch) / 'probe'
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'),
                    '--build-dir', str(args.build_dir), '--output-dir', str(out), '--build-only'],
                   cwd=root, check=True, capture_output=True, text=True)
    return probe_binary(out)


def fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def source_commit():
    """The measured tree's commit (cluster trees check out the pinned SHA); '' outside a checkout."""
    done = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=root, capture_output=True, text=True)
    return done.stdout.strip() if done.returncode == 0 else ''


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
    if args.sample_cycle:
        env['DUNECITY_SIM_SAMPLE_CYCLE'] = str(args.sample_cycle)
    if args.headless_assets:
        env['DUNECITY_HEADLESS_ASSETS'] = '1'
    if args.heap_profile:
        env['BALANCE_HEAP_PROFILE'] = '1'
    if args.heap_tools:
        env['BALANCE_HEAP_TOOLS'] = '1'
    if args.malloc_stacks and sys.platform == 'darwin':
        env['DUNECITY_SIM_MALLOC_STACKS'] = 'lite'
    if args.no_heap_relief:
        env['DUNECITY_HEADLESS_HEAP_RELIEF'] = '0'
    stress = game['case'] == STRESS
    case, bench = (stress_arguments(args, game['seed']), {}) if stress else case_arguments(game['case'])
    # A 256x256 six-house game runs far longer than the fixed cases.
    wall_timeout = max(args.wall_timeout, 4 * 3600) if stress else args.wall_timeout
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--capture-mib', '256',
           '--path-workers', '1', '--no-ai-telemetry', '--wall-timeout', str(wall_timeout)] + case
    if args.ai_telemetry:
        # Decision records are built and written inside QuantBot updates, so a stress game timed
        # with telemetry on also times the logging; its ai.* scope totals come with it.
        cmd.remove('--no-ai-telemetry')
    if stress:
        env.update(BALANCE_PERF='1', BALANCE_PERF_CYCLES=str(args.stress_minutes * GAME_PERF.CYCLES_PER_MINUTE),
                   BALANCE_PERF_FRAME_CYCLES='10', BALANCE_UNIT_PHASE_TIMING='1')
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
    perf = next((json.loads(line.split('GAME_PERF_RESULT: ', 1)[1]) for line in lines if 'GAME_PERF_RESULT: ' in line), None)
    digests = [fields(line, 'PROBE_DIGEST:') for line in lines if 'PROBE_DIGEST:' in line]
    markers = ('SCENARIO_RESULT:', 'SCENARIO_HOUSE:', 'FORMATION_DUEL_RESULT:', 'FORMATION_DUEL_HOUSE:',
               'FORMATION_DUEL_MAP:', 'FORMATION_WAVE', 'FORMATION_HOUSE:', 'CAMPAIGN_BALANCE_RESULT:')
    signature = {'digests': digests, 'results': [line.split('INFO: ', 1)[-1].strip() for line in lines
                                              if any(marker in line for marker in markers)]}
    error = '' if rc == 0 and native and timing and digests and digests[-1].get('final') == '1' else f'rc={rc};incomplete'
    if stress and perf is None:
        error = ';'.join(filter(None, (error, 'perf_missing')))
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
               signature=json.dumps(signature, separators=(',', ':')), bench='', source=source_commit(),
               stress=json.dumps(dict(map=STRESS_MAP.name, mod=args.stress_mod, minutes=args.stress_minutes,
                                      seed=game['seed'], style=args.style or ''), separators=(',', ':')) if stress else '',
               perf=json.dumps(perf, separators=(',', ':')) if perf else '',
               profile=json.dumps(native['profile'], separators=(',', ':')) if native.get('profile') else '',
               error=error)
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
    stress = [row for row in rows if row['case'] == STRESS and row['perf']]
    if stress:
        print('\n| game | style | host | cycles | units max | sim CPU ms/tick p50/p95/p99/max | QuantBot CPU ms/tick '
              'p50/p95/max | worst house QuantBot ms | calibration ms | peak RSS MiB |')
        print('|---|---|---|---:|---:|---|---|---:|---:|---:|')
        for row in sorted(stress, key=lambda row: row['id']):
            perf, setup = json.loads(row['perf']), json.loads(row['stress'])
            cpu, ai = perf['cpu'], perf['ai_cpu']
            print(f'| {row["id"]} | {setup["style"] or "native"} | {row["host"]} | {perf["cycles"]} | {perf["units_max"]} '
                  f'| {cpu["p50"]:.2f}/{cpu["p95"]:.2f}/{cpu["p99"]:.2f}/{cpu["max"]:.1f} '
                  f'| {ai["p50"]:.2f}/{ai["p95"]:.2f}/{ai["max"]:.1f} '
                  f'| {max(h["max"] for h in perf["house_cpu"].values()):.1f} | {perf["calibration_ms"]:.2f} '
                  f'| {row["peak_rss_mib"]} |')
    expected = {game['id'] for game in schedule(args)}
    return bool(any(row['error'] for row in rows) or expected != {row['id'] for row in rows})


def compare(first, second):
    """Identical digests and result lines per case (per case and seed for stress256)."""
    def load(folder):
        result = {}
        for row in read(Path(folder) / 'games.csv'):
            if row['error']:
                raise ValueError('Failed game: ' + row['id'])
            result.setdefault(row['id'].rsplit('/', 1)[0], []).append(json.loads(row['signature']))
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


def perf_measures(row):
    """A stress game's thread CPU in ms: p95 QuantBot and simulation CPU per tick, and the
    perf_regression budgets' tick, frame and worst-update figures."""
    perf = json.loads(row['perf'])
    return dict(ai_p95=perf['ai_cpu']['p95'], sim_p95=perf['cpu']['p95'], tick_p99=perf['cpu']['p99'],
                frame_p99=perf['frame_cpu']['p99'], house_max=max(house['max'] for house in perf['house_cpu'].values()))


def perf_gate(native_folder, candidate_folder):
    """The unit/combat promotion check (docs/ai-baseline-gate.md): one line, exit 0 PASS, 1 FAIL, 2 INVALID."""
    budgets = dict(tick_p99=GAME_PERF.EIGHT_BUDGETS['cpu_p99'], frame_p99=GAME_PERF.EIGHT_BUDGETS['frame_cpu_p99'],
                   house_max=GAME_PERF.BUDGETS['house_max'])

    def load(folder):
        games = {}
        for row in read(Path(folder) / 'games.csv'):
            if row['case'] != STRESS:
                continue
            if row['error'] or not row['perf']:
                raise ValueError(f'failed game {row["id"]} in {folder}')
            if not json.loads(row['perf']).get('ai_cpu_measured'):
                raise ValueError(f'{row["id"]} has no QuantBot CPU measurement')
            setup = json.loads(row['stress'])
            games[setup.pop('seed')] = (row, setup)
        return games

    try:
        native, candidate = load(native_folder), load(candidate_folder)
        if sorted(native) != sorted(candidate) or len(native) < len(STRESS_SEEDS):
            raise ValueError(f'seeds differ or fewer than {len(STRESS_SEEDS)}: {sorted(native)} vs {sorted(candidate)}')
        games = list(native.values()) + list(candidate.values())
        sources = {row['source'] for row, _ in games}
        if len(sources) != 1 or not next(iter(sources)):
            raise ValueError('arms are not from one source commit: ' + ', '.join(sorted(sources)))
        setups = {json.dumps({**setup, 'style': None}, sort_keys=True) for _, setup in games}
        if len(setups) != 1:
            raise ValueError('arms played different stress settings: ' + '; '.join(sorted(setups)))
        if {setup['style'] for _, setup in native.values()} & {setup['style'] for _, setup in candidate.values()}:
            raise ValueError('candidate and native play the same style')
        hosts = {row['host'] for row, _ in games}
        if len(hosts) != 1:
            raise ValueError('games ran on several hosts (' + ', '.join(sorted(hosts)) + '); submit both arms to one machine')
    except (ValueError, KeyError, json.JSONDecodeError) as error:
        print(f'INVALID: {error}')
        return 2
    # One host, so thread CPU in ms compares directly. Each game's calibration loop runs once at
    # start-up and read twice its usual time in 2 of 12 games on a loaded host, so the budgets
    # (multiples of the calibration loop) use the median calibration of all the games.
    calibration = statistics.median(json.loads(row['perf'])['calibration_ms'] for row, _ in games)
    seeds = sorted(native)
    base = {seed: perf_measures(native[seed][0]) for seed in seeds}
    cand = {seed: perf_measures(candidate[seed][0]) for seed in seeds}
    ratio = {key: statistics.geometric_mean(cand[seed][key] / base[seed][key] for seed in seeds)
             for key in ('ai_p95', 'sim_p95')}
    failures = [f'{name} x{ratio[key]:.3f} > x{PERF_GATE_RATIO:.2f}'
                for key, name in (('ai_p95', 'QuantBot p95'), ('sim_p95', 'sim p95')) if ratio[key] > PERF_GATE_RATIO]
    # Budgets on the seeds' geometric mean: the candidate stays inside every budget native keeps,
    # and where native already exceeds one it may not exceed native by more than the ratio limit.
    shown = []
    for key, budget in budgets.items():
        native_value = statistics.geometric_mean(base[seed][key] for seed in seeds) / calibration
        candidate_value = statistics.geometric_mean(cand[seed][key] for seed in seeds) / calibration
        shown.append(f'{key} {native_value:.2f}->{candidate_value:.2f} (budget {budget:g})')
        if candidate_value > max(budget, native_value * PERF_GATE_RATIO):
            failures.append(f'{key} {candidate_value:.2f} over budget {budget:g}')
    print(f'{"FAIL" if failures else "PASS"}, QuantBot CPU p95/tick x{ratio["ai_p95"]:.3f}, sim CPU p95/tick '
          f'x{ratio["sim_p95"]:.3f} (geometric mean over {len(seeds)} seeds on {next(iter(hosts))}, limit '
          f'x{PERF_GATE_RATIO:.2f}); budgets x calibration {calibration:.2f} ms, native->candidate: ' + ', '.join(shown)
          + (' -- ' + '; '.join(failures) if failures else ''))
    return 1 if failures else 0


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
parser.add_argument('--cases', default=','.join(DEFAULT_CASES))
parser.add_argument('--label', default='simulation')
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--sample-seconds', type=float, default=0)
parser.add_argument('--heap-profile', action='store_true')
parser.add_argument('--heap-tools', action='store_true')
parser.add_argument('--malloc-stacks', action='store_true')
parser.add_argument('--no-heap-relief', action='store_true', help='Keep freed allocator pages for an explicit control')
parser.add_argument('--headless-assets', action='store_true')
parser.add_argument('--ai-telemetry', action='store_true')
parser.add_argument('--runner-profile', action='store_true', help='Retain a cProfile table for the Python launcher')
parser.add_argument('--report', action='store_true')
parser.add_argument('--compare', nargs=2)
parser.add_argument('--perf-gate', nargs=2, metavar=('NATIVE', 'CANDIDATE'),
                    help='Unit/combat promotion check on two stress256 results directories')
parser.add_argument('--bench-export', type=Path)
parser.add_argument('--stress-seeds', type=int, nargs='+', default=list(STRESS_SEEDS))
parser.add_argument('--stress-minutes', type=int, default=50, help='stress256: game minutes measured')
parser.add_argument('--stress-mod', choices=('vanilla', 'dunecity'), default='vanilla')
parser.add_argument('--style', help='stress256: the play style (tests/ai/play-styles/<style>.ini) every QuantBot plays')
parser.add_argument('--sample-cycle', type=int, default=0,
                    help='With --sample-seconds: start macOS sample once the game reaches this cycle')
args = parser.parse_args()
if args.compare:
    sys.exit(compare(*args.compare))
if args.perf_gate:
    sys.exit(perf_gate(*args.perf_gate))
if not args.results:
    parser.error('--results is required')
if args.bench_export:
    args.bench_export.write_text(''.join(row['bench'] + '\n' for row in read(Path(args.results) / 'games.csv') if row['bench']))
elif args.report:
    sys.exit(report(args))
else:
    sys.exit(run(args))
