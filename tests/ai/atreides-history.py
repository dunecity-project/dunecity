#!/usr/bin/env python3
"""Baseline-only AAA adapter: old native objects and old campaign loader, no engine changes."""
import argparse
import configparser
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from historical_profile import finish_game, probe_binary, require_free_space, scratch_dir

root = Path(__file__).resolve().parents[2]
SEEDS = (1756374161, 906213928, 41562270, 1841712204, 1430976924, 1804669015, 1702260251)
GAME_FIELDS = ('id', 'seed', 'mod', 'winner_team', 'minutes', 'won', 'wall_s', 'error',
               'engine_revision', 'source_commit', 'probe_sha256', 'options_sha256', 'human_seat', 'harvester_override')
SAMPLE_VALUES = ('minute', 'cycle', 'house', 'team', 'alive', 'credits', 'army_value', 'army_units',
                 'harvesters', 'structures', 'factories', 'kill_value', 'loss_value')
SAMPLE_FIELDS = ('id',) + SAMPLE_VALUES
HOUSE_FIELDS = ('id', 'house', 'team', 'players', 'alive', 'credits', 'army_value', 'army_units',
                'harvesters', 'structures', 'kill_value', 'loss_value', 'first_defeated_sample_minute')
WAVE_FIELDS = ('id', 'house', 'event', 'cycle', 'members', 'value')
lock = threading.Lock()


def read(path):
    if not path.exists():
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


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def schedule(args):
    return [dict(id=f'aaa/qb-v-4ah-saved/Dune2R/{seed}', seed=seed) for seed in args.seeds]


def options_for(args, out):
    if args.harvester_limit is None:
        return Path(args.game_options)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(args.game_options)
    config['Game Options']['Maximum Number of Harvesters Override'] = str(args.harvester_limit)
    path = out.with_suffix('.options.ini')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w') as handle:
        config.write(handle)
    return path


def command(args, binary, game, out, minutes):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--output-dir', str(out), '--mod', 'Dune2R', '--custom-map',
           str(root / 'data/maps/singleplayer/5P - 128x128 - All against Atreides.ini'),
           '--house', 'atreides', '--roster', 'harkonnen:1,atreides:2,ordos:1,fremen:1,sardaukar:1',
           '--partner-difficulty', 'brutal', '--enemy-ai', 'ai-player', '--enemy-difficulty', 'hard',
           '--seed', str(game['seed']), '--minutes', str(minutes), '--elimination-match',
           '--game-options', str(options_for(args, out)), '--capture-mib', '512',
           '--wall-timeout', str(args.wall_timeout), '--path-workers', '1']
    if not args.human_seat:
        cmd += ['--local-spectator']
    if args.style:
        cmd += ['--scenario-style', args.style]
    if binary:
        cmd += ['--prebuilt-probe', str(binary)]
    return cmd


def build_probe(args, scratch):
    if args.prebuilt_probe:
        return args.prebuilt_probe
    out = scratch / 'probe'
    subprocess.run(command(args, None, schedule(args)[0], out, 1), cwd=root, check=True,
                   capture_output=True, text=True)
    binary = probe_binary(out)
    shutil.rmtree(out / 'profile', ignore_errors=True)
    return binary


def play(args, scratch, binary, game):
    out = scratch / f'g{game["seed"]}'
    started = time.monotonic()
    process = subprocess.run(command(args, binary, game, out, args.minutes), cwd=root,
                             capture_output=True, text=True)
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((parse(line, 'ELIMINATION_MATCH_RESULT:') for line in lines
                   if 'ELIMINATION_MATCH_RESULT:' in line), {})
    samples = [dict(id=game['id'], **parse(line, 'ELIMINATION_SAMPLE:'))
               for line in lines if 'ELIMINATION_SAMPLE:' in line]
    row = dict(id=game['id'], seed=game['seed'], mod='Dune2R', winner_team=result.get('winner_team', ''),
               minutes=result.get('minutes', ''), won=int(result.get('winner_team') == '2'),
               wall_s=round(time.monotonic()-started, 3),
               error='' if process.returncode == 0 and result and samples else f'rc={process.returncode};missing_result={int(not result)}',
               engine_revision=args.engine_revision,
               source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
               probe_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
               options_sha256=hashlib.sha256(options_for(args, out).read_bytes()).hexdigest(),
               human_seat=int(args.human_seat),
               harvester_override=args.harvester_limit if args.harvester_limit is not None else -1)
    results = Path(args.results)
    if row['error']:
        (results / f'failed-{game["seed"]}.txt').write_text(process.stdout + '\n' + process.stderr + '\n' + '\n'.join(lines[-80:]))
    else:
        rosters = {data['house']: data for data in (parse(line, 'ELIMINATION_ROSTER:')
                   for line in lines if 'ELIMINATION_ROSTER:' in line)}
        final = {}
        eliminated = {}
        for sample in samples:
            final[sample['house']] = sample
            if sample['alive'] == '0':
                eliminated.setdefault(sample['house'], sample['minute'])
        houses = [dict(sample, players=rosters.get(house, {}).get('players', ''),
                       first_defeated_sample_minute=eliminated.get(house, '')) for house, sample in final.items()]
        waves = []
        events = next(out.rglob('events.jsonl'), None)
        if events:
            for line in events.open(errors='replace'):
                event = json.loads(line)
                if event['event'] in ('ground_hunt','defence_response','base_scramble','army_recall'):
                    waves.append(dict(id=game['id'], house=event['house'], event=event['event'], cycle=event['cycle'],
                                      members=event['data'].get('members',''), value=event['data'].get('value','')))
        append(results / 'samples.csv', SAMPLE_FIELDS, samples)
        append(results / 'houses.csv', HOUSE_FIELDS, houses)
        append(results / 'waves.csv', WAVE_FIELDS, waves)
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, bool(row['error']))
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    for row in rows:
        print(f"{row['seed']}: team={row['winner_team']} minutes={row['minutes']} error={row['error']}")
    print(f"{sum(row['won']=='1' for row in rows)}/{len(rows)} Atreides wins")


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {row['id'] for row in read(results / 'games.csv') if not row['error']}
    todo = [game for game in schedule(args) if game['id'] not in done]
    if todo:
        require_free_space()
        with scratch_dir('atreides-history', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                list(pool.map(lambda game: play(args, scratch, binary, game), todo))
    report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--source-root', type=Path)
parser.add_argument('--prebuilt-probe', type=Path)
parser.add_argument('--style')
parser.add_argument('--results', required=True)
parser.add_argument('--engine-revision', required=True)
parser.add_argument('--game-options', type=Path, default=root / 'tests/ai/all-against-atreides-dune2r-options.ini')
parser.add_argument('--minutes', type=int, default=60)
parser.add_argument('--seeds', type=int, nargs='+', default=list(SEEDS))
parser.add_argument('--jobs', type=int, default=3)
parser.add_argument('--wall-timeout', type=int, default=1500)
parser.add_argument('--scratch')
parser.add_argument('--human-seat', action='store_true')
parser.add_argument('--harvester-limit', type=int)
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
if args.source_root:
    root = args.source_root.resolve()
if args.report:
    report(args)
else:
    run(args)
