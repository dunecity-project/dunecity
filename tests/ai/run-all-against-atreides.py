#!/usr/bin/env python3
"""quantbot_all_against_atreides: the mandatory QuantBot regression match.

Stefan's setup, played to the end on the real engine, headless: vanilla, the bundled
"5P - 128x128 - All against Atreides" map (Atreides 100,000 credits against four
500-credit houses with prebuilt bases), Atreides = QuantBot Brutal, Ordos, Harkonnen,
Sardaukar and Fremen = AI Player (hard), his Game Options (tests/ai/all-against-atreides-options.ini:
unit and harvester overrides 0, concrete required, explored map, worms respawn ...),
watched by a local spectator so no human player is involved.

Pass: on every seed Atreides eliminates every other house within --win-minutes of game
time. The limit comes from the last good release (see docs/quantbot-all-against-atreides.md);
a slow win is the regression Stefan reported, so "eventually wins" is not enough.

Each seed prints winner, game time, Atreides' credits/army/harvester curve, enemies alive,
attack waves launched (ground_hunt: count, size) and defence responses (defence_response,
base_scramble). Results and logs stay under --output-dir. The attack and defence lines are read
from the AI decision log, so it stays on unless --no-ai-telemetry; once a finished seed's lines
are in results.json its decision log (70-110 MB) is deleted unless --keep-telemetry. Path search
runs inline (--path-workers 1), the fastest setting when the matches fill the cores
(docs/headless-performance.md). A batch refuses to start below the free-space floor
(DUNECITY_MIN_FREE_GB, default 25 GB).
"""
import argparse
import json
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import discard_profile_assets, require_free_space  # noqa: E402

MAP = root / 'data/maps/singleplayer/5P - 128x128 - All against Atreides.ini'
OPTIONS = root / 'tests/ai/all-against-atreides-options.ini'
ATREIDES = 1
# Stefan's MBA game (1792142078850970-0) and five further fixed match seeds. The outcome of
# one seed is chaotic (his own game and its replay diverge at 12 minutes and end 46 minutes
# apart), so six seeds are needed before a lost or slow game is a signal.
DEFAULT_SEEDS = (1756374161, 906213928, 41562270, 1841712204, 1430976924, 1804669015)
# Stefan's lobby: slot order and teams as his game had them (Atreides alone on team 2).
ROSTER = 'harkonnen:1,atreides:2,ordos:1,fremen:1,sardaukar:1'

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--seeds', type=int, nargs='+', default=list(DEFAULT_SEEDS))
parser.add_argument('--jobs', type=int, default=3, help='Matches played at once')
parser.add_argument('--win-minutes', type=float, default=40.0,
                    help='Atreides must have eliminated every other house by this game minute')
parser.add_argument('--minutes', type=int, default=0,
                    help='Game-time cap per match (default: --win-minutes + 1, the result is decided then)')
parser.add_argument('--wall-timeout', type=int, default=1500, help='Wall seconds per match')
parser.add_argument('--harvester-limit', type=int, default=None,
                    help='Override the fixture\'s Maximum Number of Harvesters Override (e.g. -1 = unchecked)')
parser.add_argument('--enemy-ai', choices=('ai-player', 'quantbot'), default='ai-player')
parser.add_argument('--report-only', action='store_true', help='Never fail: print the table only (version comparisons)')
parser.add_argument('--human-seat', action='store_true',
                    help='An idle human player shares Atreides instead of a local spectator (releases before 1.0.820 '
                         'have no spectator mode)')
parser.add_argument('--path-workers', type=int, default=1,
                    help='Pathfinding threads per match (DUNECITY_PATH_WORKERS); results are identical for any count')
parser.add_argument('--no-ai-telemetry', action='store_true',
                    help='No AI decision log: ~3%% less CPU, but no attack/defence lines in the report')
parser.add_argument('--keep-telemetry', action='store_true',
                    help="Keep each finished seed's AI decision log after its lines were extracted")
args = parser.parse_args()
require_free_space()
cap = args.minutes or int(args.win_minutes) + 1
out = args.output_dir.resolve()
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)

options = OPTIONS
if args.harvester_limit is not None:
    text = OPTIONS.read_text().replace('Maximum Number of Harvesters Override = 0',
                                       f'Maximum Number of Harvesters Override = {args.harvester_limit}')
    options = out / 'game-options.ini'
    options.write_text(text)


def campaign(output, *extra):
    return [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
            '--output-dir', str(output), '--mod', 'vanilla', '--custom-map', str(MAP), '--house', 'atreides',
            '--roster', ROSTER, '--partner-difficulty', 'brutal',
            *(() if args.human_seat else ('--local-spectator',)), '--elimination-match',
            '--enemy-ai', args.enemy_ai, '--enemy-difficulty', 'hard', '--game-options', str(options),
            '--capture-mib', '512', '--path-workers', str(args.path_workers),
            *(('--no-ai-telemetry',) if args.no_ai_telemetry else ()), *extra]


def fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def play(seed):
    match = out / f'seed-{seed}'
    started = time.time()
    run = subprocess.run(campaign(match, '--prebuilt-probe', str(binary), '--seed', str(seed), '--minutes', str(cap),
                                  '--wall-timeout', str(args.wall_timeout)), cwd=root, capture_output=True, text=True)
    wall = round(time.time() - started)
    log = match / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((fields(l, 'ELIMINATION_MATCH_RESULT:') for l in lines if 'ELIMINATION_MATCH_RESULT:' in l), None)
    samples = [fields(l, 'ELIMINATION_SAMPLE:') for l in lines if 'ELIMINATION_SAMPLE:' in l]
    events = next(match.rglob('events.jsonl'), None)
    attacks, defences, scrambles, recalls = [], 0, 0, 0
    if events:
        for line in events.open(errors='replace'):
            try:
                row = json.loads(line)
            except ValueError:
                continue
            if row.get('house') != ATREIDES:
                continue
            if row['event'] == 'ground_hunt':
                attacks.append((row['cycle'], row['data'].get('members', 0), row['data'].get('value', 0)))
            defences += row['event'] == 'defence_response'
            scrambles += row['event'] == 'base_scramble'
            recalls += row['event'] == 'army_recall'
    minutes = float(result['minutes']) if result else None
    won = bool(result) and result['winner_team'] == result['team']
    game = {'seed': seed, 'won': won, 'minutes': minutes, 'wall_s': wall, 'result': result,
            'error': '' if run.returncode == 0 and result else f'rc={run.returncode}: {run.stderr[-600:]}',
            'samples': samples, 'attacks': attacks, 'defence_responses': defences, 'base_scrambles': scrambles,
            'army_recalls': recalls}
    if match.is_dir():
        (match / 'result.json').write_text(json.dumps(game, indent=1) + '\n')
        if not game['error'] and not args.keep_telemetry:
            discard_profile_assets(match / 'profile', telemetry=True)
    return game


def report(game):
    seed = game['seed']
    passed = game['won'] and game['minutes'] is not None and game['minutes'] <= args.win_minutes
    if game['error']:
        print(f'seed {seed}: ERROR {game["error"]}')
        return False
    result = game['result']
    winner = 'Atreides' if game['won'] else ('none (time limit)' if result['winner_team'] == '0' else 'enemies')
    print(f'seed {seed}: {"PASS" if passed else "FAIL"} winner={winner} game={game["minutes"]:.1f} min '
          f'(limit {args.win_minutes:g}) wall={game["wall_s"]} s defeated={result["defeated"]}')
    print('  min | Atreides credits  army(units)  harvesters structures | enemies alive (army value)')
    by_minute = {}
    for sample in game['samples']:
        by_minute.setdefault(int(sample['minute']), {})[int(sample['house'])] = sample
    last = max(by_minute) if by_minute else 0
    shown = sorted({m for m in by_minute if m % 5 == 0} | {last})
    for minute in shown:
        houses = by_minute[minute]
        a = houses.get(ATREIDES, {})
        enemies = [h for h in houses if h != ATREIDES and houses[h]['alive'] == '1']
        enemy_army = sum(int(houses[h]['army_value']) for h in enemies)
        print(f'  {minute:3d} | {a.get("credits", "-"):>16} {a.get("army_value", "-"):>6}({a.get("army_units", "-"):>3}) '
              f'{a.get("harvesters", "-"):>11} {a.get("structures", "-"):>10} | {len(enemies)} ({enemy_army})')
    waves = game['attacks']
    if waves:
        print(f'  attacks: {len(waves)} waves, members mean {sum(w[1] for w in waves) / len(waves):.0f} '
              f'max {max(w[1] for w in waves)}, value mean {sum(w[2] for w in waves) / len(waves):.0f} '
              f'max {max(w[2] for w in waves)}; first at {waves[0][0] / 3750:.1f} min')
    else:
        print('  attacks: none launched')
    print(f'  defence: {game["defence_responses"]} defence responses, {game["base_scrambles"]} base scrambles, '
          f'{game["army_recalls"]} army recalls')
    return passed


# One probe build, then every seed reuses it.
probe = out / 'probe'
subprocess.run(campaign(probe, '--minutes', '1', '--seed', str(args.seeds[0])), cwd=root, check=True,
               capture_output=True, text=True)
binary = probe / 'balance-probe.app/Contents/MacOS/balance-probe'
shutil.rmtree(probe / 'profile', ignore_errors=True)
started = time.time()
with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
    games = list(pool.map(play, args.seeds))
(out / 'results.json').write_text(json.dumps(games, indent=1) + '\n')
print(f'quantbot_all_against_atreides: {len(games)} seeds, {round(time.time() - started)} s wall, '
      f'harvester override {args.harvester_limit if args.harvester_limit is not None else "0 (fixture)"}')
passed = [report(game) for game in games]
print(f'{sum(passed)}/{len(passed)} seeds won within {args.win_minutes:g} game minutes')
if not all(passed) and not args.report_only:
    sys.exit(1)
