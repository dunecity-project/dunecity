#!/usr/bin/env python3
"""Native 1.0.796/807 recovery references, never a policy port (cluster-only games).

Definitions are frozen from d253e015 in ai-recovery-fixtures/manifest.json. AAA
uses the pre-820 idle HumanPlayer sharing Atreides; campaign uses one registered
QuantBot and detached UI only, retaining native authored victory and quotas.
Skirmish uses exact current-generated custom-map bytes with native TechLevel.
No engine/AI object is rebuilt specially: only the private main is compiled.

CSV adapter: games/houses/samples use the union of the frozen scenario-matrix
and dunecity-audit schemas, plus explicit native-release provenance. IDs, factors,
outcomes, elapsed minutes, team placement/score/drawn/won retain their original
meanings. Unobserved diagnostics are empty, never zero. --report DIR... prints
side-by-side family outcomes; --compare CONTROL HISTORICAL pairs exact IDs and
prints context/option/content differences. Across releases this is a whole-native
reference, NOT a same-engine causal policy comparison. Native option fields
absent in a historical release are enumerated, never emulated or silently hidden.

Submit each family with dunecity-queue --rev BRIDGE_SHA -- python3
 tests/ai/ai-recovery.py --family aaa|campaign|skirmish --results DIR.
--smoke selects the first exact scheduled case of each requested family; it does
not redraw/reseed the sample. --list and --report/--compare never build or play.
"""
import argparse
import configparser
import csv
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
from collections import defaultdict

root = Path(__file__).resolve().parents[2]
fixtures = root / 'tests/ai/ai-recovery-fixtures'
spec = importlib.util.spec_from_file_location('recovery_native_league', root / 'tests/ai/ai-baseline-league.py')
league = importlib.util.module_from_spec(spec)
spec.loader.exec_module(league)
sys.path.insert(0, str(root / 'tests/ai'))
import match_score

MANIFEST = json.loads((fixtures / 'manifest.json').read_text())
HOUSE_NUMBERS = league.HOUSE_NUMBERS
CLASSES = {'QB': 'qBotBrutal', 'QH': 'qBotHard', 'QM': 'qBotMedium', 'QE': 'qBotEasy', 'AH': 'AIPlayerHard',
           'brutal': 'qBotBrutal', 'hard': 'qBotHard', 'medium': 'qBotMedium', 'easy': 'qBotEasy'}
EXTRA = ('release', 'baseline_release_source', 'family', 'host', 'cycle', 'map_sha256', 'requested_options_sha256',
         'unsupported_options', 'native_options_hash', 'initial_native_tech', 'observer', 'comparability')
def fields(table):
    return tuple(dict.fromkeys(MANIFEST['schemas']['scenario'][table] + MANIFEST['schemas']['sample'][table] + list(EXTRA)))
GAME_FIELDS, HOUSE_FIELDS, SAMPLE_FIELDS = (fields(table) for table in ('GAME', 'HOUSE', 'SAMPLE'))
_original_probe_source = league.probe_source
scratch_dir = league.scratch_dir
finish_game = league.finish_game

def read(path):
    path = Path(path)
    if path.is_file():
        with path.open(newline='') as source:
            return list(csv.DictReader(source))
    compressed = path.with_name(path.name + '.gz')
    if compressed.is_file():
        with gzip.open(compressed, 'rt', newline='') as source:
            return list(csv.DictReader(source))
    return []

def append(path, columns, rows):
    path = Path(path)
    fresh = not path.exists()
    with path.open('a', newline='') as out:
        writer = csv.DictWriter(out, columns, extrasaction='ignore')
        if fresh:
            writer.writeheader()
        writer.writerows(rows)

def schedule(args):
    games = [dict(g) for g in MANIFEST['games'] if g['family'] in args.family]
    if args.smoke:
        seen, smoke = set(), []
        for game in games:
            if game['family'] not in seen:
                seen.add(game['family']); smoke.append(game)
        games = smoke
    if args.shard:
        k, n = map(int, args.shard.split('/'))
        if not 1 <= k <= n:
            raise ValueError('Use --shard K/N with 1 <= K <= N')
        games = games[k-1::n]
    return games

def recovery_source():
    source = _original_probe_source()
    original = (root / 'tests/ai/ai-baseline-observer.inc').as_posix()
    recovery = (root / 'tests/ai/ai-recovery-observer.inc').as_posix()
    return source.replace('#include "' + original + '"', '#include "' + recovery + '"').replace(
        'int menuResult = runAIBaselineProbe();', 'int menuResult = runAIRecoveryProbe();')

def build_probe(args, scratch):
    league.probe_source = recovery_source
    return league.build_probe(args, scratch)

def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)

def option_receipt(path):
    config = configparser.ConfigParser(interpolation=None)
    config.read(path)
    source = (root / 'src/globals.cpp').read_text()
    supported = {key.lower() for key in re.findall(r'read(?:Bool|Int)\("([^"]+)"', source)}
    unsupported = {key: value for key, value in config.items('Game Options') if key not in supported}
    return unsupported

def metadata(game):
    out = {key: value for key, value in game.items() if not isinstance(value, (dict, list, tuple))}
    out['limit'] = game['minutes']
    out['map'] = game['map']
    out['expect'] = ':'.join(map(str, game.get('expect') or []))
    return out

def play(args, scratch, binary, game):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    token = hashlib.sha256(game['id'].encode()).hexdigest()[:16]
    out = Path(scratch) / ('recovery-' + token)
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    source_commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    native = json.loads((root / 'tests/ai/baseline-native.json').read_text())
    row = dict(metadata(game), source_commit=source_commit, baseline_release_source=native['source_commit'],
               release=native['release'], host=platform.node(), error='', outcome='', winner_team='',
               comparability='whole_native_release', attack_telemetry=0, research_enabled=0,
               observer='idle_shared_atreides_human' if game['family'] == 'aaa' else 'detached_unregistered_UI')
    row['sides'] = ' '.join('%s:%s:%s' % tuple(side) for side in game['sides'])
    row['probe_sha256'] = hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    failed = True
    try:
        options = fixtures / game['options_file']
        row['requested_options_sha256'] = hashlib.sha256(options.read_bytes()).hexdigest()
        row['unsupported_options'] = json.dumps(option_receipt(options), sort_keys=True)
        profile = out / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\nFullscreen = false\n[General]\nPlay Intro = false\nDiagnostic Logs = true\n')
        env = dict(os.environ, DUNECITY_USERDIR=str(profile), DUNECITY_AI_TELEMETRY='0', SDL_VIDEODRIVER='dummy',
                   SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1',
                   RECOVERY_FAMILY=game['family'], RECOVERY_SEED=str(game['seed']),
                   RECOVERY_MINUTES=str(game['minutes']), RECOVERY_HOUSE=str(game['candidate_house']),
                   RECOVERY_TECH=str(game['initial_tech']), RECOVERY_OPTIONS=str(options),
                   RECOVERY_NATIVE_OPTIONS=str(out / 'native-options.ini'),
                   RECOVERY_DEPLOY_MCV=str(int(game.get('base') == 'cy')))
        for key in ('DUNECITY_QUANTBOT_OVERLAY', 'BALANCE_SCENARIO_STYLE', 'BALANCE_GENOME_PHASE_DIR'):
            env.pop(key, None)
        if game['family'] == 'campaign':
            env['RECOVERY_MISSION'] = str(game['mission'])
        else:
            path = fixtures / game['map_file']
            row['map_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
            if game.get('map_sha256') and row['map_sha256'] != game['map_sha256']:
                raise ValueError('Frozen map checksum changed')
            env.update(RECOVERY_MAP=str(path), RECOVERY_MAP_NAME=Path(game['map_file']).stem,
                       RECOVERY_SIDES=','.join('%s:%s:%s' % (HOUSE_NUMBERS[h], team, CLASSES[code])
                                               for h, team, code in game['sides']))
        with (out / 'run.log').open('w') as log:
            process = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=args.wall_timeout)
        lines = (out / 'run.log').read_text(errors='replace').splitlines()
        setup = [parse(line, 'RECOVERY_SETUP:') for line in lines if 'RECOVERY_SETUP:' in line]
        endings = [parse(line, 'RECOVERY_RESULT:') for line in lines if 'RECOVERY_RESULT:' in line]
        final = [parse(line, 'RECOVERY_HOUSE:') for line in lines if 'RECOVERY_HOUSE:' in line]
        if process.returncode or len(setup) != 1 or len(endings) != 1 or not final:
            raise RuntimeError('native_rc=%s; setup=%s result=%s houses=%s' %
                               (process.returncode, len(setup), len(endings), len(final)))
        setup, ending = setup[0], endings[0]
        if setup['seed'] != str(game['seed']) or setup['city_sim'] != '0':
            raise ValueError('Native setup does not match recovery definition')
        if str(game['candidate_house']) not in {house['house'] for house in final}:
            raise ValueError('Missing candidate house result')
        if game['family'] != 'campaign' and {(house['house'], house['team']) for house in final} != {
                (str(HOUSE_NUMBERS[h]), str(team)) for h, team, _ in game['sides']}:
            raise ValueError('Native house/team roster differs from frozen definition')
        row.update(ending)
        row.update({key: setup[key] for key in ('config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash')})
        row.update(native_options_hash=setup['options_hash'], initial_native_tech=setup['tech_level'],
                   eliminations=sum(house['alive'] == '0' for house in final), city_pop=0,
                   win_type=('quota' if int(ending['quota']) > 0 and int(ending['win_flags']) & 4 and
                             any(h['alive'] == '1' for h in final if h['team'] != '1') else 'conquest')
                             if game['family'] == 'campaign' and ending['outcome'] == 'won' else '')
        digests = [line.split('PROBE_DIGEST:', 1)[1].strip() for line in lines if 'PROBE_DIGEST:' in line]
        row['digest'] = digests[-1] if digests else ''
        row['winner'] = '+'.join(sorted({house['players'] for house in final if house['team'] == row['winner_team']}))
        ids = [int(house['house']) for house in final]
        eliminated = {int(house['house']): float(house['eliminated_s']) for house in final if house['alive'] == '0'}
        teams = {int(house['house']): int(house['team']) for house in final}
        placing = match_score.score_game(ids, eliminated, teams=teams) if len(set(teams.values())) > 1 else {}
        codes = {HOUSE_NUMBERS[h]: code for h, _, code in game['sides'] if h in HOUSE_NUMBERS}
        house_rows = []
        for house in final:
            number = int(house['house'])
            place = placing.get(number)
            item = dict(metadata(game), **house)
            item.update(code=codes.get(number, 'enemy'), difficulty=codes.get(number, 'campaign'),
                        place=place.place if place else '', score=round(place.score, 4) if place else '',
                        drawn=int(place.drawn) if place else '', won=int(house['team'] == row['winner_team']),
                        attack_telemetry=0, release=native['release'], source_commit=source_commit)
            house_rows.append(item)
        samples = [dict(metadata(game), **parse(line, 'RECOVERY_SAMPLE:')) for line in lines if 'RECOVERY_SAMPLE:' in line]
        append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
        append(results / 'samples.csv', SAMPLE_FIELDS, samples)
        failed = False
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        row['error'] = str(error)
        if isinstance(error, subprocess.TimeoutExpired):
            row['outcome'] = 'wall_timeout'
        else:
            row['outcome'] = 'crash'
        log = (out / 'run.log').read_text(errors='replace') if (out / 'run.log').is_file() else ''
        (results / ('failed-' + token + '.txt')).write_text(game['id'] + '\n' + row['error'] + '\n' + log[-18000:])
    finally:
        row['wall_s'] = round(time.monotonic() - started, 3)
        native_options = out / 'native-options.ini'
        receipt = dict(game=game, result=row, native_options=native_options.read_text() if native_options.is_file() else '')
        (results / ('receipt-' + token + '.json')).write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        append(results / 'games.csv', GAME_FIELDS, [row])
        finish_game(scratch, out, failed)
    return row

def count_rows(rows):
    return (sum(not r.get('error') and r['outcome'] == 'won' for r in rows),
            sum(not r.get('error') and r['outcome'] == 'lost' for r in rows),
            sum(not r.get('error') and r['outcome'] == 'time_limit' for r in rows),
            sum(bool(r.get('error')) for r in rows))

def report(args):
    directories = args.report_dirs or [args.results]
    print('| Run | Family | Games | Wins | Losses | Timeouts | Crashes/errors | AAA <=50m |')
    print('|---|---|---:|---:|---:|---:|---:|---:|')
    for directory in directories:
        rows = read(Path(directory) / 'games.csv')
        for family in ('aaa', 'campaign', 'skirmish'):
            family_rows = [r for r in rows if r.get('family', r.get('category')) == family or
                           family == 'skirmish' and r['id'].startswith('sample/vanilla/')]
            if not family_rows:
                continue
            counts = count_rows(family_rows)
            gate = sum(not r.get('error') and r['outcome'] == 'won' and float(r['minutes']) <= 50 for r in family_rows) if family == 'aaa' else ''
            print('| %s | %s | %s | %s | %s | %s | %s | %s |' % ((Path(directory).name, family, len(family_rows)) + counts + (gate,)))
    if not args.report_dirs:
        good = {r['id'] for r in rows if not r.get('error')}
        missing = {g['id'] for g in schedule(args)} - good
        return bool(missing or any(r.get('error') for r in rows))
    return False

def compare(args):
    controls, native = ({r['id']: r for r in read(Path(directory) / 'games.csv')} for directory in args.compare_dirs)
    print('Whole-native-release reference; different content/engine/options are disclosed, not policy-isolated evidence.')
    print('| Family | Common IDs | Control W/L/T/E | Native W/L/T/E | Option hashes differ | Content hashes differ |')
    print('|---|---:|---|---|---:|---:|')
    for family in ('aaa', 'campaign', 'skirmish'):
        keys = [key for key, row in native.items() if row.get('family') == family and key in controls]
        if not keys:
            continue
        groups = [[source[key] for key in keys] for source in (controls, native)]
        differences = [sum(controls[key].get(field) != native[key].get(field) for key in keys)
                       for field in ('options_hash', 'content_hash')]
        print('| %s | %s | %s | %s | %s | %s |' %
              ((family, len(keys)) + tuple('/'.join(map(str, count_rows(group))) for group in groups) + tuple(differences)))
    houses = [{(row['id'], row['house']): row for row in read(Path(directory) / 'houses.csv')}
              for directory in args.compare_dirs]
    strata = defaultdict(list)
    factors = ('format', 'difficulty', 'size', 'spice_level', 'initial_tech', 'candidate_seat')
    for key, row in native.items():
        if not key.startswith('sample/vanilla/') or key not in controls:
            continue
        if any(row.get(factor) != controls[key].get(factor) for factor in factors):
            continue
        strata[('all', 'all')].append(key)
        for factor in factors:
            strata[(factor, row[factor])].append(key)
    if strata:
        print('\nNative-context sample references; no identical-rule regression inference:')
        print('| Factor | Stratum | Matched IDs | Control W/L/T/E | Native W/L/T/E | Mean score control -> native | Strict option exclusions |')
        print('|---|---|---:|---|---|---|---:|')
        for (factor, level), keys in sorted(strata.items()):
            means = []
            for source, observed in zip((controls, native), houses):
                values = [float(observed[(key, source[key]['candidate_house'])]['score']) for key in keys
                          if not source[key].get('error') and (key, source[key]['candidate_house']) in observed
                          and observed[(key, source[key]['candidate_house'])].get('score', '') != '']
                means.append('%+.3f (%d/%d)' % (sum(values) / len(values), len(values), len(keys))
                             if values else 'unmeasured')
            counts = ['/'.join(map(str, count_rows([source[key] for key in keys])))
                      for source in (controls, native)]
            excluded = sum(native[key].get('unsupported_options', '{}') != '{}' for key in keys)
            print('| %s | %s | %s | %s | %s | %s | %s |' %
                  (factor, level, len(keys), counts[0], counts[1], ' -> '.join(means), excluded))
    print('Missing tactical/economy metrics remain empty. Context differences are enumerated per game.')

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results')
parser.add_argument('--family', nargs='+', choices=('aaa', 'campaign', 'skirmish'), default=['aaa', 'campaign', 'skirmish'])
parser.add_argument('--build-dir', type=Path, default=root / 'build-int')
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--smoke', action='store_true')
parser.add_argument('--shard')
parser.add_argument('--list', action='store_true')
parser.add_argument('--report', nargs='+', dest='report_dirs')
parser.add_argument('--compare', nargs=2, dest='compare_dirs')
args = parser.parse_args()
if args.compare_dirs:
    compare(args)
elif args.report_dirs:
    report(args)
elif args.list:
    for game in schedule(args):
        print(game['id'], json.dumps(game, sort_keys=True))
else:
    raise SystemExit('Games run only through a commit-pinned dunecity-queue submission')
