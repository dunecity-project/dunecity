#!/usr/bin/env python3
"""Native campaign difficulty calibration, not a gameplay gate or a human skill simulation.

Primary: idle HumanPlayer + one Both-role QuantBot on the player house.
Diagnostic: sole QuantBot + real houseless observer, with the SAME native objectives.
The native CampaignPlayerHouse config is not changed: the diagnostic can use enemy-style
caps and is deliberately never pooled with the human-partner denominator.

Defaults schedule 4,224 primary games or 924 paired support-path diagnostics. All 66
actual missions, two mods, fixed shared fresh seeds; native assets/rules are untouched.
Use --list-json for exact metadata; submit real games through dunecity-queue --rev.
"""
import argparse
import ast
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/ai'))
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, headless_asset_arguments, require_free_space, scratch_dir  # noqa: E402
from fleet_observer import fleet_observations  # noqa: E402


def scenario_adapter():
    """Reuse the existing adapter's declaration API without executing its CLI or gate schedule."""
    path = root / 'tests/ai/scenario-matrix.py'
    tree = ast.parse(path.read_text(encoding='utf-8'), str(path))
    body = []
    for node in tree.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        body.append(node)
    else:
        raise RuntimeError('Scenario adapter has no CLI boundary')
    namespace = {'__file__': str(path), '__name__': 'campaign_difficulty_scenario_adapter'}
    exec(compile(ast.Module(body=body, type_ignores=[]), str(path), 'exec'), namespace)
    return namespace


matrix = scenario_adapter()
TIERS = ('easy', 'medium', 'hard', 'brutal')
MODS = ('vanilla', 'dunecity')
HOUSES = ('atreides', 'harkonnen', 'ordos')
SEEDS = (2611092101, 2611092102)
CLASSES = dict(zip(TIERS, ('qBotEasy', 'qBotMedium', 'qBotHard', 'qBotBrutal')))
CODES = dict(zip(TIERS, ('QE', 'QM', 'QH', 'QB')))
META = ('id', 'category', 'cell', 'seed', 'mod', 'phase', 'support_mode', 'player_difficulty',
        'enemy_difficulty', 'enemy_ai', 'player_house', 'level', 'mission')
GAME_EXTRA = ('outcome_class', 'censored', 'lose_flags', 'native_victory', 'controller_check',
              'observing', 'registered_humans', 'objective_house', 'resolved_roster',
              'config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash',
              'slow_city_year', 'fog_of_war', 'campaign_player_house', 'controllers_json',
              'telemetry_status', 'telemetry_bytes', 'wave_completeness', 'capture_cap_mib',
              'diagnostic_path', 'sample_rows', 'refinery_rows', 'wave_rows')
LOGISTICS = ('harvesters_total', 'harvesters_active', 'harvesters_harvesting', 'harvesters_returning',
             'refinery_bays_busy', 'tax_per_min', 'tax_rate')


def fields(*groups):
    return tuple(dict.fromkeys(key for group in groups for key in group))


GAME_FIELDS = fields(META, matrix['GAME_FIELDS'], GAME_EXTRA)
HOUSE_FIELDS = fields(META, matrix['HOUSE_FIELDS'])
SAMPLE_FIELDS = fields(META, matrix['SAMPLE_FIELDS'], LOGISTICS)
REFINERY_FIELDS = fields(META, matrix['REFINERY_FIELDS'])
WAVE_FIELDS = fields(META, matrix['WAVE_FIELDS'], ('data_json',))
read = matrix['read']
append = matrix['append']
parse = matrix['parse']


def schedule(args):
    if not args.seeds or len(set(args.seeds)) != len(args.seeds) or any(not 0 <= s <= 0xffffffff for s in args.seeds):
        raise SystemExit('--seeds must contain distinct unsigned 32-bit seeds')
    if len(set(args.enemy_difficulties)) != len(args.enemy_difficulties):
        raise SystemExit('--enemy-difficulties must be distinct')
    games = []
    # Easy enemy cells first; no mission/seed rerolling or outcome-dependent selection.
    for enemy in TIERS:
        if enemy not in args.enemy_difficulties:
            continue
        for player in TIERS:
            if args.phase == 'diagnostic' and TIERS.index(player) - TIERS.index(enemy) not in (0, 1):
                continue
            if args.phase == 'smoke' and (player, enemy) != ('easy', 'easy'):
                continue
            modes = ('human-partner', 'ai-only') if args.phase == 'smoke' else (
                ('human-partner',) if args.phase == 'primary' else ('ai-only',))
            seeds = args.seeds if args.phase == 'primary' else args.seeds[:1]
            missions = matrix['campaign_missions']() if args.phase != 'smoke' else [(1, 1)]
            houses = HOUSES if args.phase != 'smoke' else ('harkonnen',)
            for support in modes:
                for mod in MODS:
                    for house in houses:
                        for level, mission in missions:
                            for seed in seeds:
                                cell = f'player-{player}-enemy-{enemy}/{support}/{mod}'
                                game = dict(id=f'campaign-difficulty/{cell}/{house}-m{mission}/{seed}',
                                            category='campaign', cell=cell, kind='campaign',
                                            phase=args.phase, support_mode=support, mod=mod, seed=seed,
                                            house=house, player_house=house, level=level, mission=mission,
                                            player_difficulty=player, enemy_difficulty=enemy, enemy_ai='quantbot',
                                            partner=player, ai_only=support == 'ai-only', minutes=args.minutes,
                                            map=f'{house} level {level} mission {mission}', expect=None,
                                            sides=[(house, 1, CODES[player]), ('enemies', 2, CODES[enemy])])
                                if not args.cases or re.search(args.cases, game['id']):
                                    games.append(game)
    if len({game['id'] for game in games}) != len(games):
        raise RuntimeError('Duplicate campaign difficulty schedule IDs')
    return games


def metadata(game):
    return {key: game[key] for key in META}


def command(args, binary, game, out):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--scenario', '--mod', game['mod'],
           '--house', game['house'], '--level', str(game['level']), '--mission', str(game['mission']),
           '--partner-difficulty', game['player_difficulty'], '--enemy-ai', 'quantbot',
           '--enemy-difficulty', game['enemy_difficulty'], '--seed', str(game['seed']),
           '--minutes', str(game['minutes']), '--capture-mib', '256', '--wall-timeout', str(args.wall_timeout),
           '--path-workers', '1']
    cmd += headless_asset_arguments()
    if game['ai_only']:
        cmd += ['--campaign-ai-only']
    if args.scenario_style:
        cmd += ['--scenario-style', args.scenario_style]
    return cmd


def build_probe(args, scratch):
    return matrix['build_probe'](args, scratch)


def markers(lines, marker):
    return [parse(line, marker + ':') for line in lines if marker + ':' in line]


def retain_diagnostics(results, out, token, controllers, setup):
    target = results / 'diagnostics' / token
    target.mkdir(parents=True, exist_ok=True)
    if (out / 'run.log').exists():
        with (out / 'run.log').open('rb') as source, gzip.open(target / 'run.log.gz', 'wb') as dest:
            shutil.copyfileobj(source, dest)
    for name in ('setup.json', 'summary.json', 'effective-options.ini'):
        if (out / name).exists():
            shutil.copyfile(out / name, target / name)
    (target / 'controllers.json').write_text(json.dumps({'setup': setup, 'controllers': controllers}, indent=2) + '\n')
    return str(target.relative_to(results))


def play(args, scratch, binary, game):
    token = hashlib.sha256(game['id'].encode()).hexdigest()[:20]
    out = scratch / ('g' + token)
    started = time.monotonic()
    env = dict(os.environ)
    # A native baseline never inherits a sibling experiment, saved options or observer regression.
    for name in list(env):
        if name.startswith('BALANCE_') or name in ('DUNECITY_QUANTBOT_OVERLAY', 'DUNECITY_TECH_SAVINGS_AUDIT'):
            env.pop(name)
    env.update(BALANCE_DUNECITY_AUDIT='1', BALANCE_CAMPAIGN_DIFFICULTY_AUDIT='1',
               BALANCE_SCENARIO_DUMP_OPTIONS=str(out / 'effective-options.ini'))
    process = subprocess.run(command(args, binary, game, out), cwd=root, capture_output=True, text=True, env=env)
    wall = round(time.monotonic() - started, 3)
    lines = (out / 'run.log').read_text(errors='replace').splitlines() if (out / 'run.log').exists() else []
    results = Path(args.results)
    result_rows = markers(lines, 'SCENARIO_RESULT')
    result = result_rows[0] if len(result_rows) == 1 else {}
    houses = markers(lines, 'SCENARIO_HOUSE')
    setups = markers(lines, 'CAMPAIGN_DIFFICULTY_SETUP')
    setup = setups[0] if len(setups) == 1 else {}
    controllers = markers(lines, 'CAMPAIGN_DIFFICULTY_CONTROLLER')
    native_setup = next(iter(markers(lines, 'SCENARIO_SETUP')), {})
    digests = [item for item in markers(lines, 'PROBE_DIGEST') if item.get('final') == '1']
    summary_file = out / 'summary.json'
    summary = json.loads(summary_file.read_text()) if summary_file.exists() else {}
    errors = []
    if process.returncode:
        errors.append(f'rc={process.returncode}')
    if not result or result.get('mode') != 'campaign' or not digests:
        errors.append('missing_native_campaign_result_or_digest')
    expected = {'support_mode': game['support_mode'], 'player': CLASSES[game['player_difficulty']],
                'enemy': CLASSES[game['enemy_difficulty']], 'objective_house': str(matrix['HOUSE_NUMBERS'][game['house']]),
                'registered_humans': '0' if game['ai_only'] else '1', 'observing': str(int(game['ai_only'])),
                'native_victory': '1', 'controller_check': '1'}
    if not controllers or any(setup.get(key) != value for key, value in expected.items()):
        errors.append('controller_or_native_objective_mismatch')
    raw = result.get('outcome', '')
    win_type = matrix['win_type'](result, houses, True)
    classification = 'error' if errors else win_type if raw == 'won' else 'defeat' if raw == 'lost' else 'draw'
    if raw not in ('won', 'lost', 'time_limit') and not errors:
        errors.append('unknown_native_outcome')
        classification = 'error'
    row = dict(metadata(game), map=game['map'], limit=game['minutes'],
               sides=' '.join(f'{h}:{t}:{code}' for h, t, code in game['sides']), expect='',
               outcome=raw, outcome_class=classification, censored=int(raw == 'time_limit' or bool(errors)),
               winner_team=result.get('winner_team', ''), winner='', end_s=result.get('end_s', ''),
               minutes=result.get('minutes', ''), eliminations=sum(h.get('alive') == '0' for h in houses),
               win_type=win_type, digest=digests[0].get('digest', '') if digests else '', wall_s=wall,
               error=';'.join(errors), source_commit=summary.get('sourceCommit', ''),
               source_fingerprint=summary.get('sourceFingerprint', ''), style_seats=matrix['style_seats'](lines),
               control_diagnostics=0, endgame_audit=json.dumps(markers(lines, 'CAMPAIGN_ENDGAME_AUDIT'), separators=(',', ':')),
               resolved_roster=json.dumps(markers(lines, 'SCENARIO_ROSTER'), separators=(',', ':')),
               controllers_json=json.dumps(controllers, separators=(',', ':')), capture_cap_mib=256,
               wave_completeness='not_certified')
    for key in ('triggers', 'fired', 'win_flags', 'lose_flags', 'quota', 'city_pop'):
        row[key] = result.get(key, '')
    for key in ('native_victory', 'controller_check', 'observing', 'registered_humans', 'objective_house',
                'slow_city_year', 'fog_of_war', 'campaign_player_house'):
        row[key] = setup.get(key, '')
    for key in ('config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash'):
        row[key] = native_setup.get(key, '')
    row['diagnostic_path'] = retain_diagnostics(results, out, token, controllers, setup)
    house_rows = []
    for house in houses:
        code = CODES[game['player_difficulty']] if int(house['house']) == matrix['HOUSE_NUMBERS'][game['house']] else CODES[game['enemy_difficulty']]
        house_rows.append(dict(metadata(game), **house, code=code))
    samples = {}
    for marker in ('ELIMINATION_SAMPLE', 'DUNECITY_AUDIT', 'DUNECITY_ECONOMY', 'DUNECITY_LOGISTICS'):
        for sample in markers(lines, marker):
            sample.update(json.loads(sample.pop('economy', '{}')))
            samples.setdefault((sample['house'], sample['cycle']), {}).update(sample)
    fleet_samples, refinery_samples = fleet_observations(lines)
    for key, sample in fleet_samples.items():
        samples.setdefault((str(key[0]), str(key[1])), {}).update(sample)
    events = next(out.rglob('events.jsonl'), None) if out.exists() else None
    waves = []
    row['telemetry_status'] = 'captured' if events else 'missing'
    row['telemetry_bytes'] = events.stat().st_size if events else ''
    if events:
        with events.open(errors='replace') as handle:
            for line in handle:
                event = json.loads(line)
                if event['event'] in ('ground_hunt', 'defence_response', 'base_scramble', 'army_recall'):
                    data = event.get('data', {})
                    waves.append(dict(metadata(game), house=event['house'], event=event['event'], cycle=event['cycle'],
                                      members=data.get('members', ''), value=data.get('value', ''),
                                      data_json=json.dumps(data, sort_keys=True, separators=(',', ':'))))
    row.update(sample_rows=len(samples), refinery_rows=len(refinery_samples), wave_rows=len(waves))
    append(results / 'samples.csv', SAMPLE_FIELDS, [dict(metadata(game), **sample) for sample in samples.values()])
    append(results / 'refinery-samples.csv', REFINERY_FIELDS, [dict(metadata(game), **sample) for sample in refinery_samples])
    append(results / 'waves.csv', WAVE_FIELDS, waves)
    append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
    if errors:
        (results / f'failed-{token}.txt').write_text(game['id'] + '\n' + process.stdout[-4000:] + '\n' + process.stderr[-4000:])
    # The existing queue collector treats games.csv as the commit record; append it last.
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, bool(errors))
    return row


def report(args):
    rows = {row['id']: row for row in read(Path(args.results) / 'games.csv')}
    expected = {game['id'] for game in schedule(args)}
    selected = [rows[key] for key in expected if key in rows]
    counts = Counter(row['outcome_class'] for row in selected)
    missing = expected - rows.keys()
    print(f'Campaign difficulty {args.phase}: {len(selected)}/{len(expected)} recorded; '
          + ', '.join(f'{key}={value}' for key, value in sorted(counts.items())))
    return bool(missing or any(row.get('error') for row in selected))


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {row['id'] for row in read(results / 'games.csv') if not row.get('error')}
    todo = [game for game in schedule(args) if game['id'] not in done]
    if todo:
        require_free_space()
        with scratch_dir('campaign-difficulty', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, game) for game in todo]
                for future in as_completed(futures):
                    row = future.result()
                    print(f'{row["id"]} -> {row["outcome_class"]} {row["error"]}', flush=True)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--phase', choices=('primary', 'diagnostic', 'smoke'), default='primary')
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--results')
parser.add_argument('--scratch')
parser.add_argument('--seeds', nargs='+', type=int, default=list(SEEDS))
parser.add_argument('--enemy-difficulties', nargs='+', choices=TIERS, default=list(TIERS))
parser.add_argument('--minutes', type=int, choices=range(1, 121), default=120)
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--cases', help='Explicit diagnostic ID filter; never changes the default full study')
parser.add_argument('--scenario-style',
                    help='Candidate arm: tests/ai/play-styles/<style>.ini applied to every QuantBot '
                         '(enemy-only switches leave the player proxy unchanged); omit for native')
parser.add_argument('--list-json', action='store_true')
args = parser.parse_args()
if args.list_json:
    print(json.dumps(schedule(args)))
elif not args.results:
    parser.error('--results is required')
elif args.wall_timeout < 1 or args.jobs < 1:
    parser.error('--wall-timeout and --jobs must be positive')
else:
    sys.exit(int(run(args)))
