#!/usr/bin/env python3
"""Real-engine six-edition campaign/custom QA; the cluster schedules one game per unit.

The optional --baseline-root runs identical nonfamily isolation cases against an existing,
read-only main checkout/build. All profiles and diagnostic products remain in leased scratch.
Audit markers retain actual production, damage, roster, mission and terrain observations;
fixtures do not issue gameplay commands or alter AI rules.
"""
import argparse
import configparser
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir

FAMILY = ('Tornie', 'TornieLite', 'Jericho', 'JerichoLite', 'ArcaKev', 'ArcaKevLite')
HOUSES = ('harkonnen', 'atreides', 'ordos', 'fremen', 'sardaukar', 'mercenary',
          'neutral', 'rebels', 'custom', 'wildspade', 'kleshmersh', 'tharpique')
MISSIONS = ((1, 1), (5, 11), (8, 20), (8, 21), (9, 22))
GAME_FIELDS = ('id', 'category', 'mod', 'house', 'level', 'mission', 'seed', 'minutes', 'outcome',
               'cycle', 'digest', 'checkpoints', 'opponents', 'expected_opponents', 'player_production',
               'enemy_production', 'production_note', 'lilac', 'white', 'log_sha256', 'wall_s', 'error')
HOUSE_FIELDS = ('id', 'house', 'team', 'started', 'alive', 'units', 'structures', 'built_units',
                'built_structures', 'kills', 'losses', 'initial_factories', 'initial_operating_factories')
SAMPLE_FIELDS = ('id', 'marker', 'data')
lock = threading.Lock()


def read(path):
    path = Path(path)
    if not path.is_file():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    with lock:
        new = not path.exists()
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def playable(mod):
    ini = configparser.ConfigParser(interpolation=None, strict=False)
    ini.read(root / 'mods' / mod / 'mod.ini')
    return [HOUSES[int(h)] for h in ini['Content Options']['Playable Houses'].split(',')]


def schedule(args):
    games = []
    if 'campaign' in args.categories:
        for mod in args.mods:
            for house in playable(mod):
                for level, mission in MISSIONS:
                    games.append(dict(id=f'campaign/{mod}/{house}/m{mission:03d}/42', category='campaign',
                                      mod=mod, house=house, level=level, mission=mission, seed=42, minutes=10))
    if 'custom' in args.categories:
        for mod in args.mods:
            for difficulty in ('hard', 'brutal'):
                games.append(dict(id=f'custom/{mod}/{difficulty}/546', category='custom', mod=mod,
                                  house=playable(mod)[0], houses=playable(mod), difficulty=difficulty,
                                  level=9, mission=0, seed=546, minutes=25, coloured=difficulty == 'brutal'))
    if 'isolation' in args.categories:
        for mod in ('vanilla', 'dunecity', 'Dune2R'):
            for seed in (54601, 54602, 54603):
                games.append(dict(id=f'isolation/{mod}/{seed}', category='isolation', mod=mod,
                                  house='harkonnen', houses=list(HOUSES[:4]), difficulty='hard',
                                  level=9, mission=0, seed=seed, minutes=20, coloured=False))
    if args.case_filter:
        games = [g for g in games if re.search(args.case_filter, g['id'])]
    return games


def engine_root(args):
    return Path(args.baseline_root).resolve() if args.baseline_root else root


def build_probe(args, scratch):
    out = Path(scratch) / 'probe'
    if args.baseline_root:
        # The ordinary runner writes balance-probe-cache inside its build. This
        # comparison promises a read-only baseline, so only compile a private main.
        import fcntl
        baseline, build = engine_root(args), Path(args.build_dir).resolve()
        out.mkdir()
        target = 'bin/dunecity.app/Contents/MacOS/dunecity'
        commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
        main = (baseline / 'src/main.cpp').read_text()
        needle = 'int menuResult = MainMenu().showMenu();'
        if main.count(needle) != 1:
            raise RuntimeError('Baseline main-menu diagnostic injection point changed')
        main = main.replace(needle, 'int menuResult = runCampaignBalanceProbe();')
        main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
        position = main.index('int main(')
        main = main[:position] + '#include "' + str(baseline / 'tests/ai/campaign-balance-probe.inc') + '"\n' + main[position:]
        source, obj = out / 'balance-main.cpp', out / 'balance-main.o'
        source.write_text(main)
        cc = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
        for option in ('-include', '-MT', '-MF'):
            if option in cc:
                index = cc.index(option)
                del cc[index:index + 2]
        for option in ('-MD', '-MMD'):
            if option in cc:
                cc.remove(option)
        cc[cc.index('-o') + 1] = str(obj)
        cc[cc.index('-c') + 1] = str(source)
        cc.append('-fno-access-control')
        app = out / 'balance-probe.app/Contents'
        (app / 'MacOS').mkdir(parents=True)
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
        binary = app / 'MacOS/balance-probe'
        link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
        link = link[link.index('&&') + 1:]
        link = link[:link.index('&&')]
        link[link.index('-o') + 1] = str(binary)
        link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
        with open('/tmp/dunecity-tornie-546.build.lock', 'a') as gate, (out / 'build.log').open('w') as log:
            fcntl.flock(gate, fcntl.LOCK_EX)
            subprocess.run(['nice', '-n', '10', *cc], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(['nice', '-n', '10', *link], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        provenance = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=baseline, text=True).strip(),
                          source_root=str(baseline), build=str(build), compile=cc, link=link,
                          binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        (Path(args.results) / 'baseline-build.json').write_text(json.dumps(provenance, indent=2) + '\n')
        return binary
    command = [sys.executable, str(engine_root(args) / 'tests/ai/run-campaign-balance.py'),
               '--build-dir', str(args.build_dir), '--output-dir', str(out), '--scenario',
               '--skirmish', 'size=64,base=cy,army=0,seed=546',
               '--scenario-sides', 'harkonnen:1:qBotEasy,atreides:2:qBotEasy',
               '--local-spectator', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry']
    if sys.platform != 'win32':
        command = ['nice', '-n', '10'] + command
        if shutil.which('flock'):
            command = ['flock', '/tmp/dunecity-tornie-546.build.lock'] + command
    done = subprocess.run(command, cwd=engine_root(args), capture_output=True, text=True)
    if done.returncode:
        raise RuntimeError(f'Probe build failed ({done.returncode}): {done.stdout}\n{done.stderr}; evidence {out}')
    return probe_binary(out)


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker + ':', 1)[1].split() if '=' in part)


def command(args, binary, game, out):
    cmd = [sys.executable, str(engine_root(args) / 'tests/ai/run-campaign-balance.py'),
           '--build-dir', str(args.build_dir), '--prebuilt-probe', str(binary), '--output-dir', str(out),
           '--scenario', '--mod', game['mod'], '--seed', str(game['seed']), '--minutes', str(game['minutes']),
           '--house', game['house'], '--capture-mib', '256', '--wall-timeout', str(args.wall_timeout),
           '--path-workers', '1', '--no-ai-telemetry']
    if game['category'] == 'campaign':
        return cmd + ['--level', str(game['level']), '--mission', str(game['mission']),
                      '--partner-difficulty', 'hard', '--enemy-ai', 'campaign']
    difficulty = 'qBotBrutal' if game['difficulty'] == 'brutal' else 'qBotHard'
    sides = ','.join(f'{house}:{i + 1}:{difficulty}' for i, house in enumerate(game['houses']))
    size = (256 if len(game['houses']) == 12 else 192) if game['category'] == 'custom' else 96
    base = 'large' if game['category'] == 'custom' else 'small'
    return cmd + ['--local-spectator', '--scenario-sides', sides, '--skirmish',
                  f'size={size},base={base},army=0,tech=9,credits=10000,rock=3,spice=4,seed={game["seed"]}']


def play(args, scratch, binary, game):
    out = Path(scratch) / ('g-' + hashlib.sha256(game['id'].encode()).hexdigest()[:16])
    started = time.monotonic()
    env = dict(os.environ, BALANCE_DIGEST_CYCLES='3000')
    if not args.baseline_root:
        env['BALANCE_TORNIE_AUDIT'] = '1'
    if game.get('coloured'):
        env['BALANCE_TORNIE_COLOURED_SPICE'] = '1'
    done = subprocess.run(command(args, binary, game, out), cwd=engine_root(args), env=env,
                          capture_output=True, text=True)
    log_path = out / 'run.log'
    log_bytes = log_path.read_bytes() if log_path.exists() else b''
    lines = log_bytes.decode(errors='replace').splitlines()
    records = [(m.group(1), parse(line, m.group(1))) for line in lines
               if (m := re.search(r'\b(TORNIE_AUDIT_[A-Z_]+|SCENARIO_[A-Z_]+|PROBE_DIGEST):', line))]
    result = next((d for m, d in records if m == 'SCENARIO_RESULT'), {})
    final = next((d for m, d in records if m == 'PROBE_DIGEST' and d.get('final') == '1'), {})
    terrain = next((d for m, d in records if m == 'TORNIE_AUDIT_MAP'), {})
    mission = next((d for m, d in records if m == 'TORNIE_AUDIT_MISSION'), {})
    authored = next((d for m, d in records if m == 'TORNIE_AUDIT_AUTHORED'), {})
    houses = [dict(id=game['id'], **d) for m, d in records if m == 'TORNIE_AUDIT_HOUSE']
    own_id = str(HOUSES.index(game['house']))
    own = next((h for h in houses if h['house'] == own_id), {})
    player_production = int(own.get('built_units', 0)) + int(own.get('built_structures', 0))
    enemy_production = sum(int(h['built_units']) + int(h['built_structures']) for h in houses
                           if h['house'] != own_id and h['started'] == '1')
    errors = []
    if done.returncode or not result or not final:
        errors.append(f'process={done.returncode};result={bool(result)};digest={bool(final)}')
    bad_lines = [line.strip() for line in lines if re.search(r'\b(?:ERROR|ASSERTION|assertion failed|Assertion.*failed)\b', line)]
    if bad_lines:
        errors.append('log-errors=' + json.dumps(bad_lines[:8]))
    expected = ''
    notes = []
    if game['category'] == 'campaign' and result:
        revised = game['mod'] in ('Tornie', 'TornieLite')
        authored_ids = {h for h in authored.get('opponents', '').split(',') if h}
        observed_ids = {h['house'] for h in houses if h['started'] == '1' and h['house'] != own_id}
        expected = (4 if game['mission'] == 22 else 3 if game['mission'] in (20, 21) else 2) if revised else len(authored_ids)
        if revised and (mission.get('recipe') != '546' or int(mission.get('declared_opponents', -1)) != expected):
            errors.append('wrong revised campaign recipe/opponent declaration')
        if int(terrain.get('opponents', -1)) != expected or observed_ids != authored_ids:
            errors.append(f'loaded opponents={sorted(observed_ids)} authored={sorted(authored_ids)} expected={expected}')
        if not player_production:
            if int(own.get('initial_operating_factories', 0)) > 0:
                errors.append('operating player factory produced nothing')
            else:
                notes.append('manual_opening_no_operating_factory')
        inactive_factories = [h['house'] for h in houses if h['house'] != own_id and h['started'] == '1'
                              and int(h.get('initial_operating_factories', 0)) > 0
                              and int(h['built_units']) + int(h['built_structures']) == 0]
        if inactive_factories:
            errors.append('factory-bearing enemies produced nothing=' + ','.join(inactive_factories))
        if float(result.get('minutes', 0)) < 8:
            notes.append('natural_' + result.get('outcome', 'end') + '_before_8m')
    if game['category'] == 'custom' and result:
        if sum(h['started'] == '1' for h in houses) != len(game['houses']):
            errors.append('wrong custom house count')
        if float(result.get('minutes', 0)) < 20:
            errors.append('custom ended before 20 minutes')
        if game.get('coloured'):
            lilac, white = int(terrain.get('lilac', 0)), int(terrain.get('white', 0))
            if (game['mod'] in ('Tornie', 'TornieLite') and (lilac == 0 or white != 0)
                    or game['mod'] not in ('Tornie', 'TornieLite') and (white == 0 or lilac != 0)):
                errors.append('edition lilac/white spice mask did not match the map')
    row = {key: game.get(key, '') for key in GAME_FIELDS}
    row.update(outcome=result.get('outcome', ''), cycle=final.get('cycle', ''), digest=final.get('digest', ''),
               checkpoints=sum(m == 'PROBE_DIGEST' for m, _ in records), opponents=terrain.get('opponents', ''),
               expected_opponents=expected, player_production=player_production, enemy_production=enemy_production,
               production_note=';'.join(notes),
               lilac=terrain.get('lilac', ''), white=terrain.get('white', ''), log_sha256=hashlib.sha256(log_bytes).hexdigest(),
               wall_s=round(time.monotonic() - started, 2), error=';'.join(errors))
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    append(results / 'samples.csv', SAMPLE_FIELDS,
           [dict(id=game['id'], marker=m, data=json.dumps(d, sort_keys=True, separators=(',', ':'))) for m, d in records])
    append(results / 'houses.csv', HOUSE_FIELDS, houses)
    if errors:
        (results / f'failed-{out.name}.txt').write_text(game['id'] + '\n' + done.stdout + '\n' + done.stderr
                                                     + '\n' + '\n'.join(bad_lines) + '\n' + '\n'.join(lines[-100:]))
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, bool(errors))
    return row


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {r['id'] for r in read(results / 'games.csv') if not r['error']}
    todo = [g for g in schedule(args) if g['id'] not in done]
    if todo:
        require_free_space()
        with scratch_dir('tornie-gameplay', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                for future in as_completed([pool.submit(play, args, scratch, binary, g) for g in todo]):
                    row = future.result()
                    print(f'{row["id"]}: {row["outcome"]} cycle={row["cycle"]} {row["error"] or "PASS"}', flush=True)
    rows = read(results / 'games.csv')
    latest = {r['id']: r for r in rows}
    failures = [r for r in latest.values() if r['error']]
    missing = [g['id'] for g in schedule(args) if g['id'] not in latest]
    print(f'Tornie gameplay: {len(latest) - len(failures)} passed, {len(failures)} failed, {len(missing)} missing')
    if failures or missing:
        raise SystemExit(1)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build-tornie'))
parser.add_argument('--results', required=True)
parser.add_argument('--scratch')
parser.add_argument('--categories', nargs='+', choices=('campaign', 'custom', 'isolation'), default=['campaign', 'custom'])
parser.add_argument('--mods', nargs='+', choices=FAMILY, default=list(FAMILY))
parser.add_argument('--case-filter')
parser.add_argument('--baseline-root', help='Existing main checkout; read-only diagnostic compilation against its build-dir')
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--list', action='store_true')
args = parser.parse_args()
if args.list:
    print(json.dumps(schedule(args), indent=2))
else:
    run(args)
