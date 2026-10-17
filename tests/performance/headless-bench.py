#!/usr/bin/env python3
"""Headless simulation benchmark: fixed real-engine duels, timed and fingerprinted.

Plays a fixed set of Random Skirmish 1v1 QuantBot duels (the formation-duel.py setup: mirrored
map, Fortress bases, 20,000-credit armies, Hard on both sides, WaveFormation None) through
run-campaign-balance.py with one prebuilt probe binary, at one or more parallelism levels.
Every game records wall time, CPU time (user+sys of the runner and the probe), peak RSS, the
probe's own setup/loop split and game cycles per second, and its PROBE_DIGEST lines
(tests/ai/probe-digest.inc). Results are appended to --results as JSON lines.

  headless-bench.py --jobs 1,2,4 --results R.jsonl          # build a probe from --build-dir, then measure
  headless-bench.py --probe P --jobs 4 --label after --results R.jsonl
  headless-bench.py --summary R.jsonl            # games/hour, per-game wall, cycles/s, RSS
  headless-bench.py --compare A.jsonl B.jsonl    # identical digests and results, game by game

See docs/headless-performance.md.
"""
import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import require_free_space, scratch_dir  # noqa: E402

# size, map/match seed, house:shape,house:shape (formation-duel.py's houses for these seeds).
GAMES = (('128', '7101', 'atreides:none,harkonnen:none'),
         ('160', '7202', 'ordos:none,atreides:none'),
         ('192', '7303', 'harkonnen:none,ordos:none'))


def build_probe(args, scratch):
    probe = scratch / 'probe'
    shutil.rmtree(probe, ignore_errors=True)
    subprocess.run(['python3', str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
                    '--output-dir', str(probe), '--duel-map-size', '64', '--duel-map-seed', '1',
                    '--duel-sides', 'atreides:none,harkonnen:none', '--local-spectator', '--enemy-difficulty', 'hard',
                    '--partner-difficulty', 'hard', '--minutes', '1', '--capture-mib', '256'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(probe / 'profile', ignore_errors=True)
    return probe / 'balance-probe.app/Contents/MacOS/balance-probe'


def field(line, name):
    for part in line.split():
        if part.startswith(name + '='):
            return part.split('=', 1)[1]
    return ''


def play(args, scratch, probe, game, jobs, index):
    size, seed, sides = game
    out = scratch / f'{args.label}-j{jobs}-{index}-{size}-{seed}'
    shutil.rmtree(out, ignore_errors=True)
    command = ['python3', str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
               '--prebuilt-probe', str(probe), '--output-dir', str(out), '--duel-map-size', size,
               '--duel-map-seed', seed, '--duel-sides', sides, '--local-spectator', '--enemy-difficulty', 'hard',
               '--partner-difficulty', 'hard', '--minutes', str(args.minutes), '--seed', seed,
               '--capture-mib', '256', '--wall-timeout', '3600']
    if args.path_workers:
        command += ['--path-workers', str(args.path_workers)]
    if not args.ai_telemetry:
        command.append('--no-ai-telemetry')
    started = time.time()
    console = out.with_suffix('.out')
    with console.open('w') as handle:
        process = subprocess.Popen(command, cwd=root, stdout=handle, stderr=subprocess.STDOUT)
        _, status, usage = os.wait4(process.pid, 0)
    returncode = os.waitstatus_to_exitcode(status)
    output = console.read_text(errors='replace')
    console.unlink()
    wall = time.time() - started
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    timing = next((l for l in lines if 'PROBE_TIMING:' in l), '')
    result = next((l for l in lines if 'FORMATION_DUEL_RESULT:' in l), '')
    row = {'label': args.label, 'jobs': jobs, 'size': size, 'seed': seed, 'sides': sides, 'minutes': args.minutes,
           'path_workers': args.path_workers, 'ai_telemetry': args.ai_telemetry,
           'rc': returncode, 'wall_s': round(wall, 2),
           'cpu_s': round(usage.ru_utime + usage.ru_stime, 2),
           # macOS reports ru_maxrss in bytes: the largest of the runner and the probe.
           'max_rss_mib': round(usage.ru_maxrss / (1 << 20) if sys.platform == 'darwin' else usage.ru_maxrss / 1024),
           'setup_ms': int(field(timing, 'setup_ms') or 0), 'loop_ms': int(field(timing, 'loop_ms') or 0),
           'cycles': int(field(timing, 'cycles') or 0), 'cycles_per_s': int(field(timing, 'cycles_per_s') or 0),
           'winner': field(result, 'winner'), 'end_s': field(result, 'end_s'),
           'digests': [f'{field(l, "cycle")}:{field(l, "digest")}' for l in lines if 'PROBE_DIGEST:' in l],
           'houses': [l.split('FORMATION_DUEL_HOUSE:', 1)[1].strip() for l in lines if 'FORMATION_DUEL_HOUSE:' in l],
           # Every wave/army measurement line: the harness's own output must not change either.
           'measurements': [l.strip() for l in lines if 'FORMATION_WAVE' in l or 'FORMATION_HOUSE:' in l
                            or 'FORMATION_DUEL_MAP:' in l]}
    if returncode != 0 or not timing:
        row['error'] = output[-2000:] + '\n'.join(lines[-30:])
    if not args.keep:
        shutil.rmtree(out, ignore_errors=True)
    return row


def measure(args):
    if not args.results:
        sys.exit('--results is required to measure.')
    require_free_space()
    with scratch_dir('headless-bench', args.scratch) as scratch:
        measure_in(args, scratch)


def measure_in(args, scratch):
    probe = Path(args.probe).resolve() if args.probe else build_probe(args, scratch)
    games = [GAMES[int(i)] for i in args.games.split(',')]
    results = Path(args.results)
    for jobs in (int(j) for j in args.jobs.split(',')):
        # Every parallelism level plays the same games, at least 'jobs' of them so all slots fill.
        batch = [games[i % len(games)] for i in range(max(len(games) * args.rounds, jobs * args.rounds))]
        # Longest first: bigger maps take longer, so they must not start last.
        order = sorted(range(len(batch)), key=lambda i: -int(batch[i][0]))
        started = time.time()
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            rows = list(pool.map(lambda i: play(args, scratch, probe, batch[i], jobs, i), order))
        elapsed = time.time() - started
        with results.open('a') as handle:
            for row in rows:
                row['batch_wall_s'] = round(elapsed, 1)
                row['batch_games'] = len(rows)
                handle.write(json.dumps(row) + '\n')
        failed = [r for r in rows if r.get('error')]
        print(f'{args.label} jobs={jobs}: {len(rows)} games in {elapsed:.0f} s = {len(rows) * 3600 / elapsed:.1f} games/h'
              f'{f" ({len(failed)} FAILED)" if failed else ""}', flush=True)
        for row in failed:
            print(row['error'], file=sys.stderr)


def summary(path):
    rows = [json.loads(l) for l in Path(path).read_text().splitlines() if l.strip()]
    print('| label | jobs | games | games/h | wall s/game | CPU s/game | loop cycles/s | setup ms | peak RSS MiB |')
    print('|---|---|---|---|---|---|---|---|---|')
    batches = {}
    for row in rows:
        batches.setdefault((row['label'], row['jobs'], row['batch_wall_s']), []).append(row)
    for (label, jobs, wall), group in batches.items():
        mean = lambda key: statistics.mean(r[key] for r in group)
        print(f'| {label} | {jobs} | {len(group)} | {len(group) * 3600 / wall:.1f} | {mean("wall_s"):.1f} | '
              f'{mean("cpu_s"):.1f} | {mean("cycles_per_s"):.0f} | {mean("setup_ms"):.0f} | '
              f'{max(r["max_rss_mib"] for r in group)} |')


def compare(first, second):
    def load(path):
        games = {}
        for line in Path(path).read_text().splitlines():
            if line.strip():
                row = json.loads(line)
                games.setdefault((row['size'], row['seed'], row['sides'], row['minutes']), []).append(row)
        return games
    a, b = load(first), load(second)
    same = True
    for key in sorted(set(a) & set(b)):
        runs = a[key] + b[key]
        signatures = {json.dumps([r['digests'], r['winner'], r['end_s'], r['houses'], r.get('measurements')])
                      for r in runs}
        status = 'identical' if len(signatures) == 1 else 'DIFFERENT'
        same &= len(signatures) == 1
        print(f'{key[0]}/{key[1]} {key[2]} {key[3]} min: {len(runs)} runs, {len(runs[0]["digests"])} digests, '
              f'winner {runs[0]["winner"] or "none"} at {runs[0]["end_s"]} s: {status}')
    missing = set(a) ^ set(b)
    if missing:
        print('Only in one file:', sorted(missing))
    return 0 if same and not missing else 1


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--probe', help='Prebuilt balance-probe binary (default: build one from --build-dir)')
parser.add_argument('--label', default='bench')
parser.add_argument('--games', default='0,1,2', help='Indices into the fixed game list (128/7101, 160/7202, 192/7303)')
parser.add_argument('--minutes', type=int, default=20)
parser.add_argument('--jobs', default='1', help='Comma-separated parallelism levels to measure')
parser.add_argument('--rounds', type=int, default=1, help='Plays each game this many times per level (min)')
parser.add_argument('--scratch', help='Directory for the probe and run directories '
                                      '(default: leased agent scratch, removed after a successful run)')
parser.add_argument('--results', help='JSON-lines file the rows are appended to (required to measure)')
parser.add_argument('--keep', action='store_true', help='Keep each run directory (use with --scratch)')
parser.add_argument('--path-workers', type=int, default=1,
                    help='DUNECITY_PATH_WORKERS for every game (1: inline, as formation-duel.py; 0: the engine default)')
parser.add_argument('--ai-telemetry', action='store_true', help='Keep the AI decision log (as ctest probes do)')
parser.add_argument('--summary', metavar='RESULTS')
parser.add_argument('--compare', nargs=2, metavar=('A', 'B'))
args = parser.parse_args()
if args.summary:
    summary(args.summary)
elif args.compare:
    sys.exit(compare(*args.compare))
else:
    measure(args)
