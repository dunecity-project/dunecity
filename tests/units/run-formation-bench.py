#!/usr/bin/env python3
"""Formation micro-benchmark (docs/formations.md, "Scenario tests").

Two fixed armies on a generated bench map, no bases, AI or production (formation-bench.inc):
army A (Atreides) attack-moves at army B (Ordos) through the real player order in one of the
five shapes or with formations off, or hunts (engine Hunt, as AI waves without a shape); B
area-guards (mode hold) or attack-moves back at once (mode attack). Every trial deploys fresh
armies at seeded, jittered positions that are the same for every shape, so shapes are compared
pairwise. Terrains (generated INI maps, 96x48):

  open       sand everywhere
  canyon     a mountain wall across the middle with a 5-tile gap (a chokepoint)
  obstacles  2x2 mountain pillars staggered across the middle third
  air        open sand; B is three Ornithopters hunting A, which attack-moves across the map

Writes <results>/bench.csv (one row per trial, appended per terrain run) and --report prints the
tables used in docs/formations.md. The probe binary is built once into --output-dir (default: leased
agent scratch, removed after a successful batch); each run's directory is deleted once its rows are
written (with --trace-s its run.log is copied to <results>/trace-<terrain>-<comp>.log first).
"""
import argparse
import csv
import os
import random
import shlex
import statistics
import shutil
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, require_free_space, scratch_dir  # noqa: E402

W, H = 96, 48
SHAPES = ('off', 'hunt', 'box', 'pincer', 'column', 'staggered', 'echelon')
FIELDS = ('terrain', 'comp', 'mode', 'trial', 'shape', 'a_units', 'b_units', 'a_value', 'b_value', 'a_lost', 'b_lost',
          'a_survivors', 'b_survivors', 'contact_s', 'resolve_s', 'a_fired', 'a_fired10', 'b_fired', 'b_fired10',
          'cross_n', 'cross_spread_s', 'cross_last_s', 'stuck_s', 'aa_units', 'aa_fired_air', 'formed_s', 'march_tps', 'pace_tps')


def bench_map(terrain):
    """The map INI and the x of the line whose crossing is timed."""
    rows = [['-'] * W for _ in range(H)]
    for x0, y0 in ((0, 0), (W - 5, H - 5)):          # rock for the two Construction Yards
        for y in range(y0, y0 + 5):
            for x in range(x0, x0 + 5):
                rows[y][x] = '%'
    line = W // 2
    if terrain == 'canyon':
        gap = range(H // 2 - 2, H // 2 + 3)
        for y in range(H):
            if y not in gap:
                for x in (W // 2 - 1, W // 2):
                    rows[y][x] = '@'
        line = W // 2 + 1
    elif terrain == 'obstacles':
        for i, x in enumerate(range(32, 64, 6)):
            for y in range(2 + (i % 2) * 3, H - 2, 6):
                for dy in (0, 1):
                    for dx in (0, 1):
                        rows[y + dy][x + dx] = '@'
    lines = ['[BASIC]', 'Version=2', 'TechLevel=8', 'WinFlags=3', 'LoseFlags=1', '', '[MAP]', f'SizeX={W}', f'SizeY={H}', '']
    lines += [f'{y:03d}=' + ''.join(row) for y, row in enumerate(rows)]
    lines += ['', '[Player1]', 'Credits=0', 'Quota=0', '', '[Player2]', 'Credits=0', 'Quota=0', '',
              '[STRUCTURES]', f'ID000=Player1,Const Yard,256,{1 * W + 1}', f'ID001=Player2,Const Yard,256,{(H - 4) * W + W - 4}', '']
    return '\n'.join(lines), line


def build(args, out):
    binary = out / 'formation-bench.app/Contents/MacOS/formation-bench'
    if binary.exists() and not args.rebuild:
        return binary
    build_dir = args.build_dir.resolve()
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build_dir)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    lines = subprocess.check_output(['ninja', '-C', str(build_dir), '-t', 'commands', target], text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main menu entry changed; update the bench injection point.')
    main = main.replace(needle, 'int menuResult = runFormationBench();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    pos = main.index('int main(')
    main = main[:pos] + '#include "' + str(root / 'tests/units/formation-bench.inc') + '"\n' + main[pos:]
    source = out / 'formation-bench-main.cpp'
    source.write_text(main)
    cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in cc:
            i = cc.index(option)
            del cc[i:i + 2]
    for option in ('-MD', '-MMD'):
        if option in cc:
            cc.remove(option)
    obj = out / 'formation-bench-main.o'
    cc[cc.index('-o') + 1] = str(obj)
    cc[cc.index('-c') + 1] = str(source)
    cc.append('-fno-access-control')
    app = out / 'formation-bench.app/Contents'
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    resources = app / 'Resources'
    if not resources.exists():
        resources.symlink_to(build_dir / 'bin/dunecity.app/Contents/Resources')
    link = shlex.split(next(line for line in lines if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(cc, cwd=build_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def run_one(args, binary, out, terrain, comps):
    work = out / f'{terrain}-{"-".join(comps)}'
    work.mkdir(parents=True, exist_ok=True)
    text, line = bench_map(terrain)
    (work / 'bench.ini').write_text(text)
    env = dict(os.environ, DUNECITY_USERDIR=str(work / 'profile'), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               DUNECITY_PATH_WORKERS='1', DUNECITY_AI_TELEMETRY='0', BENCH_MAP=str(work / 'bench.ini'),
               BENCH_TERRAIN=terrain, BENCH_COMPS=','.join(comps), BENCH_SHAPES=','.join(args.shapes),
               BENCH_MODES=','.join(args.modes if terrain != 'air' else ['hold']), BENCH_TRIALS=str(args.trials),
               BENCH_LINE_X=str(line), BENCH_LIMIT_S=str(args.limit), BENCH_TRACE_S=str(args.trace_s))
    log = work / 'run.log'
    try:
        with log.open('w') as handle:
            subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=handle,
                           stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)
        text = log.read_text(errors='replace')
        if 'FORMATION_BENCH_DONE' not in text:
            raise RuntimeError(f'Bench run {work} did not finish')
    except Exception:
        finish_game(out, work, True)
        raise
    rows = []
    for l in text.splitlines():
        if 'FORMATION_BENCH:' in l:
            rows.append(dict(part.split('=', 1) for part in l.split('FORMATION_BENCH:', 1)[1].split() if '=' in part))
    if args.trace_s:
        shutil.copyfile(log, Path(args.results) / f'trace-{work.name}.log')
    return rows, work


def play(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    require_free_space()
    with scratch_dir('formation-bench', args.output_dir) as out:
        binary = build(args, out)
        runs = [(terrain, [comp]) for terrain in args.terrains for comp in (args.air_comps if terrain == 'air' else args.comps)]
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for rows, work in pool.map(lambda r: run_one(args, binary, out, *r), runs):
                path = results / 'bench.csv'
                new = not path.exists()
                with path.open('a', newline='') as handle:
                    writer = csv.DictWriter(handle, fieldnames=FIELDS, extrasaction='ignore')
                    if new:
                        writer.writeheader()
                    writer.writerows(rows)
                finish_game(out, work, False)
                print(f'{rows[0]["terrain"]}/{rows[0]["comp"]}: {len(rows)} trials', flush=True)


def f(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def ci(values, rounds=2000):
    rng = random.Random(20261017)
    means = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(rounds))
    return means[int(0.025 * rounds)], means[int(0.975 * rounds) - 1]


def march_cell(rows):
    """Median form-up seconds and A's median march speed (mean x progress from the end of the
    form-up to contact) as a share of the group's pace; '-' for rows recorded before these fields."""
    marched = [r for r in rows if f(r.get('march_tps', -1)) >= 0 and f(r.get('pace_tps')) > 0]
    if not marched:
        return '- | -'
    formed = statistics.median(f(r['formed_s']) for r in marched)
    return f'{formed:.0f} | {100 * statistics.median(f(r["march_tps"]) / f(r["pace_tps"]) for r in marched):.0f}%'


def report(args):
    rows = list(csv.DictReader(Path(args.results, 'bench.csv').open()))
    groups = defaultdict(list)
    for r in rows:
        groups[(r['terrain'], r['comp'], r['mode'])].append(r)
    for (terrain, comp, mode), group in sorted(groups.items()):
        air = terrain == 'air'
        by_trial = defaultdict(dict)
        for r in group:
            by_trial[r['trial']][r['shape']] = r
        print(f'\n**{terrain} / {comp} / B {"hunts (ornithopters)" if air else mode}** '
              f'(A {group[0]["a_units"]} units {group[0]["a_value"]}, B {group[0]["b_units"]} units {group[0]["b_value"]}; '
              f'{len(by_trial)} trials per shape)\n')
        if air:
            print('| A shape | B lost / B value | A lost | Thopters left | AA fired at air | Resolve s | Net (B lost - A lost) | Net v off (95% CI) |')
            print('|---|---|---|---|---|---|---|---|')
        else:
            print('| A shape | A wins | Trade (B lost / A lost) | Net value | Net v off (95% CI) | A survivors | Contact s | '
                  'Resolve s | A fired ≤10 s | Cross spread s | Blocked s / member | Formed s | March / pace |')
            print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
        for shape in [s for s in SHAPES if any(s in t for t in by_trial.values())]:
            rs = [t[shape] for t in by_trial.values() if shape in t]
            net = [f(r['b_lost']) - f(r['a_lost']) for r in rs]
            diffs = [f(t[shape]['b_lost']) - f(t[shape]['a_lost']) - (f(t['off']['b_lost']) - f(t['off']['a_lost']))
                     for t in by_trial.values() if shape in t and 'off' in t]
            vs = '-' if shape == 'off' or not diffs else f'{statistics.mean(diffs):+.0f} ({ci(diffs)[0]:+.0f} to {ci(diffs)[1]:+.0f})'
            resolve = [f(r['resolve_s']) for r in rs if f(r['resolve_s']) > 0]
            if air:
                print(f'| {shape} | {statistics.mean(f(r["b_lost"]) for r in rs):.0f} / {rs[0]["b_value"]} | '
                      f'{statistics.mean(f(r["a_lost"]) for r in rs):.0f} | {statistics.mean(f(r["b_survivors"]) for r in rs):.2f} | '
                      f'{statistics.mean(f(r["aa_fired_air"]) for r in rs):.1f} / {rs[0]["aa_units"]} | '
                      f'{statistics.median(resolve) if resolve else -1:.0f} ({len(resolve)}/{len(rs)}) | '
                      f'{statistics.mean(net):+.0f} | {vs} |')
                continue
            wins = sum(1 for r in rs if f(r['b_survivors']) == 0 and f(r['a_survivors']) > 0)
            lost_a = sum(f(r['a_lost']) for r in rs)
            trade = sum(f(r['b_lost']) for r in rs) / lost_a if lost_a else float('inf')
            fired10 = sum(f(r['a_fired10']) for r in rs) / sum(f(r['a_units']) for r in rs)
            cross = [f(r['cross_spread_s']) for r in rs if f(r['cross_n']) > 0]
            print(f'| {shape} | {wins}/{len(rs)} | {trade:.2f} | {statistics.mean(net):+.0f} | {vs} | '
                  f'{statistics.mean(f(r["a_survivors"]) for r in rs):.1f} | '
                  f'{statistics.median(f(r["contact_s"]) for r in rs):.0f} | '
                  f'{statistics.median(resolve) if resolve else -1:.0f} | {100 * fired10:.0f}% | '
                  f'{statistics.median(cross) if cross else -1:.1f} | '
                  f'{statistics.mean(f(r["stuck_s"]) / max(1, f(r["a_units"])) for r in rs):.1f} | '
                  f'{march_cell(rs)} |')


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, help='Probe binary and per-run directories (logs, maps) '
                                                     '(default: leased agent scratch, removed after a successful batch)')
parser.add_argument('--results', required=True, help='Directory for bench.csv')
parser.add_argument('--terrains', nargs='+', default=['open'], choices=('open', 'canyon', 'obstacles', 'air'))
parser.add_argument('--comps', nargs='+', default=['tanks', 'mixed', 'sonic', 'light', 'heavy', 'big'])
parser.add_argument('--air-comps', nargs='+', default=['air', 'air-tanks'])
parser.add_argument('--shapes', nargs='+', default=list(SHAPES))
parser.add_argument('--modes', nargs='+', default=['hold', 'attack'])
parser.add_argument('--trials', type=int, default=12)
parser.add_argument('--limit', type=int, default=300, help='Game seconds per trial')
parser.add_argument('--trace-s', type=int, default=0,
                    help='Diagnostics: log every member of A every this many game seconds (run.log, FORMATION_BENCH_TRACE)')
parser.add_argument('--jobs', type=int, default=4)
parser.add_argument('--wall-timeout', type=int, default=7200)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
if args.report:
    report(args)
else:
    play(args)
