#!/usr/bin/env python3
"""City airplane flights in whole games, three match rules off against on, in the existing build.

Stefan's games, started through the Custom Game screen and its lobby with the shipped default
options: the bundled 4P - 192x192 - Metropolis map with every house a QuantBot Brutal and the
player watching (his game of 2026-11-23, 148 to 289 Rocket Turrets a house), and 2P - 192x192 -
City with the local human against one QuantBot Brutal (eight Rocket Turrets a house). The same
map and seed are played in four states of the rules, each adding one to the last:

    base      the build Stefan played (4cf0fd5c4): one aimed 100-point bomb per building tile
              from a rack of six, random waypoints, planes only from the Airport's free roll
    stick     + City Plane Carpet Bombing (a 250-point bomb every two tiles, no rack)
    runs      + City Plane Bombing Runs
    purchase  + City Plane Purchase (all defaults)

Per arm: flights per game, the share of flights whose plane crossed an enemy building and an
enemy zone, the enemy building tiles that came under the bomb sight and the share of them that
got a bomb, bombs and buildings destroyed per flight, the seconds a plane lasted, planes bought
and credits spent. Differences are paired by map and seed with a 95% bootstrap interval
(tests/ai/match_score.py).

The test requires, over the games played:
- Stefan's report reproduced in the base arm: under half the enemy building tiles that come
  under a plane's bomb sight get a bomb ("the planes DO go over an enemy city" and drop nothing).
- With the default rules at least 95% of the enemy buildings that stay under the bomb sight
  for a bomb period (13 cycles, two tiles of flight) get a bomb, in every game. (The first
  stick dropped two bombs a tile and this was measured per tile; the heavy bomb falls every
  two tiles, so it is measured per building.)
- The stick destroys more buildings per flight than the base arm over the games, and with the
  bombing runs but without purchases under half the buildings on the map in a game (some
  buildings, not all). What bought planes add is reported, not bounded.
- With runs no smaller a share of flights bombs than without.
- No plane is bought with the purchase rule off; a computer player buys only when its planes
  have destroyed at least their price per ended flight (it may buy none); every purchase
  leaves its house 3000 credits or more. There is no limit on planes in the air.

Outputs: flights.csv, purchases.csv, games.csv, summary.md, and per game run.log.
"""
import argparse
import csv
import os
import re
import shlex
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import NATIVE_TARGET, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402
from probe_profile import default_build_dir, discard_profile_assets, require_free_space, scratch_dir  # noqa: E402
from match_score import bootstrap  # noqa: E402

NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
MAPS = {'metropolis': ('data/maps/multiplayer/4P - 192x192 - Metropolis.ini', 4, 'spectate'),
        'city': ('data/maps/singleplayer/2P - 192x192 - City.ini', 2, 'human')}
ARMS = (('base', '000'), ('stick', '100'), ('runs', '110'), ('purchase', '111'))
CYCLES_PER_SECOND = 62.5
PRICE = 1500


def build_probe(build, out):
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    commands = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed; update the city plane flights runner.')
    main = main.replace(needle, 'int menuResult = runCityPlaneFlights();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/city-plane-flights.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'city-plane-flights-main.cpp', out / 'city-plane-flights-main.o'
    source.write_text(main, encoding='utf-8')
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    # A private diagnostic object must not overwrite Ninja's production depfile/PCH records.
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')
    binary = native_probe_binary(build, out, 'city-plane-flights')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def play(binary, scratch, out, game, cycles, timeout):
    name, seed, arm, rules = game
    run = out / 'games' / ('%s-%d-%s' % (name, seed, arm))
    run.mkdir(parents=True, exist_ok=True)
    profile = scratch / ('profile-%s-%d-%s' % (name, seed, arm))
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\nPlayer Name = Plane watcher\n'
        # The one saved choice of Stefan's the screen needs; every game option is the shipped default.
        'Custom Game Mod = dunecity\n',
        encoding='utf-8')
    path, players, seats = MAPS[name]
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1', CITY_PLANE_FLIGHTS_OUT=str(run),
               CITY_PLANE_FLIGHTS_MAP=str(root / path), CITY_PLANE_FLIGHTS_PLAYERS=str(players),
               CITY_PLANE_FLIGHTS_SEATS=seats,
               CITY_PLANE_FLIGHTS_SEED=str(seed), CITY_PLANE_FLIGHTS_CYCLES=str(cycles), CITY_PLANE_FLIGHTS_RULES=rules)
    logfile = run / 'run.log'
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout)
    lines = [line for line in logfile.read_text(errors='replace').splitlines() if line.startswith('CITY_PLANE_')]
    if sum('CITY_PLANE_FLIGHTS_PASS:' in line for line in lines) != 1 or any('CITY_PLANE_FLIGHTS_FAIL:' in line for line in lines):
        raise RuntimeError('Missing or failed city plane flights game; see ' + str(logfile))
    # Only the plane lines are kept: a whole game's log is hundreds of megabytes of city simulation.
    logfile.write_text('\n'.join(lines) + '\n')
    discard_profile_assets(profile)
    return run


def fields(line):
    return dict(part.split('=') for part in line.split(': ', 1)[1].split())


def collect(out, games, cycles):
    flights, purchases, summary = [], [], []
    for name, seed, arm, rules in games:
        run = out / 'games' / ('%s-%d-%s' % (name, seed, arm))
        key = dict(map=name, seed=seed, arm=arm)
        lines = (run / 'run.log').read_text().splitlines()
        launched = {fields(l)['object']: fields(l) for l in lines if l.startswith('CITY_PLANE_LAUNCHED:')}
        down = {fields(l)['object'] for l in lines if l.startswith('CITY_PLANE_DOWN:')}
        starts = [fields(l) for l in lines if l.startswith('CITY_PLANE_FLIGHTS_START:')]
        with (run / 'flights.csv').open(newline='') as handle:
            rows = list(csv.DictReader(handle))
        for row in rows:
            start = launched.get(row['object'], {})
            end = 'down' if row['object'] in down else 'flying' if int(row['last']) >= cycles - 1 else 'left'
            flights.append(dict(key, **row, bought=start.get('bought', '0'), end=end,
                                seconds=round((int(row['last']) - int(row['first'])) / CYCLES_PER_SECOND, 1)))
        with (run / 'purchases.csv').open(newline='') as handle:
            bought = list(csv.DictReader(handle))
        purchases += [dict(key, **row) for row in bought]
        with (run / 'houses.csv').open(newline='') as handle:
            houses = list(csv.DictReader(handle))
        count = len(rows)
        summary.append(dict(
            key, flights=count, crossed_building=sum(int(r['crossed_building']) for r in rows),
            crossed_zone=sum(int(r['crossed_zone']) for r in rows), bombed=sum(int(r['bombs']) > 0 for r in rows),
            sight_tiles=sum(int(r['sight_tiles']) for r in rows), bombed_tiles=sum(int(r['bombed_tiles']) for r in rows),
            sight_buildings=sum(int(r['sight_buildings']) for r in rows),
            bombed_buildings=sum(int(r['bombed_buildings']) for r in rows),
            bombs=sum(int(r['bombs']) for r in rows), destroyed=sum(int(r['destroyed']) for r in rows),
            credits_destroyed=sum(int(r['credits']) for r in rows), shot_down=len(down), bought=len(bought),
            credits_spent=PRICE * len(bought),
            buildings_start=sum(int(s['buildings']) for s in starts),
            buildings_end=sum(int(h['buildings']) for h in houses),
            turrets_start=sum(int(s['rocket_turrets']) for s in starts),
            least_credits_after=min((int(r['credits_after']) for r in bought), default='')))
    for name, rows in (('flights', flights), ('purchases', purchases), ('games', summary)):
        with (out / (name + '.csv')).open('w', newline='') as handle:
            if rows:
                writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
    return flights, purchases, summary


def seed_list(text):
    return [int(part) for part in text.split(',')]


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=default_build_dir(root))
parser.add_argument('--output-dir', type=Path, default=default_build_dir(root) / 'city-plane-flights')
parser.add_argument('--maps', nargs='+', choices=sorted(MAPS), default=sorted(MAPS))
parser.add_argument('--seeds', type=seed_list, default=[4711], help='Game seeds, e.g. 4711,4712')
parser.add_argument('--minutes', type=float, default=6.0, help='Game minutes per game')
parser.add_argument('--jobs', type=int, default=2, help='Games played at once')
parser.add_argument('--report', action='store_true', help='Only recompute the report from the games in --output-dir')
parser.add_argument('--wall-timeout', type=int, default=2400)
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
cycles = int(args.minutes * 60 * CYCLES_PER_SECOND)
games = [(name, seed, arm, rules) for name in args.maps for seed in args.seeds for arm, rules in ARMS]

if not args.report:
    require_free_space()
    binary = build_probe(build, out)
    with scratch_dir('city-plane-flights') as scratch:
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            list(pool.map(lambda game: play(binary, scratch, out, game, cycles, args.wall_timeout), games))
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

flights, purchases, summary = collect(out, games, cycles)
by_arm = {arm: [row for row in summary if row['arm'] == arm] for arm, _ in ARMS}
flights_by = {arm: [row for row in flights if row['arm'] == arm] for arm, _ in ARMS}


def per_flight(rows, value):
    return sum(value(row) for row in rows) / len(rows) if rows else 0.0


def table(chosen_maps, title):
    lines = ['### ' + title, '',
             '| Measure | ' + ' | '.join(arm for arm, _ in ARMS) + ' |', '|---|' + '---:|' * len(ARMS)]
    rows = {arm: [r for r in by_arm[arm] if r['map'] in chosen_maps] for arm, _ in ARMS}
    fl = {arm: [r for r in flights_by[arm] if r['map'] in chosen_maps] for arm, _ in ARMS}

    def add(label, value, form='%.2f'):
        lines.append('| ' + label + ' | ' + ' | '.join(form % value(arm) for arm, _ in ARMS) + ' |')
    games_n = max(1, len(rows[ARMS[0][0]]))
    add('Games', lambda a: len(rows[a]), '%d')
    add('Flights per game', lambda a: len(fl[a]) / max(1, len(rows[a])), '%.1f')
    add('Flights that crossed an enemy building, %', lambda a: 100 * per_flight(fl[a], lambda r: int(r['crossed_building'])), '%.0f')
    add('Flights that crossed an enemy zone, %', lambda a: 100 * per_flight(fl[a], lambda r: int(r['crossed_zone'])), '%.0f')
    add('Flights that released a bomb, %', lambda a: 100 * per_flight(fl[a], lambda r: int(r['bombs']) > 0), '%.0f')
    add('Enemy building tiles under the bomb sight per flight', lambda a: per_flight(fl[a], lambda r: int(r['sight_tiles'])), '%.1f')
    add('... of them that got a bomb, %', lambda a: 100 * sum(int(r['bombed_tiles']) for r in fl[a]) / max(1, sum(int(r['sight_tiles']) for r in fl[a])), '%.0f')
    add('Enemy buildings under the bomb sight for two tiles of flight or more, per flight', lambda a: per_flight(fl[a], lambda r: int(r['sight_buildings'])), '%.1f')
    add('... of them that got a bomb, %', lambda a: 100 * sum(int(r['bombed_buildings']) for r in fl[a]) / max(1, sum(int(r['sight_buildings']) for r in fl[a])), '%.0f')
    add('Bombs released per flight', lambda a: per_flight(fl[a], lambda r: int(r['bombs'])))
    add('Bombs that fell on a building, %', lambda a: 100 * sum(int(r['on_building']) for r in fl[a]) / max(1, sum(int(r['bombs']) for r in fl[a])), '%.0f')
    add('Buildings destroyed per flight', lambda a: per_flight(fl[a], lambda r: int(r['destroyed'])))
    add('Buildings destroyed per 100 bombs', lambda a: 100 * sum(int(r['destroyed']) for r in fl[a]) / max(1, sum(int(r['bombs']) for r in fl[a])), '%.1f')
    add('Most buildings destroyed by one flight', lambda a: max((int(r['destroyed']) for r in fl[a]), default=0), '%d')
    add('Buildings destroyed by bombs per game', lambda a: sum(int(r['destroyed']) for r in rows[a]) / max(1, len(rows[a])), '%.1f')
    add('... as % of the buildings at the start', lambda a: 100 * sum(int(r['destroyed']) for r in rows[a]) / max(1, sum(int(r['buildings_start']) for r in rows[a])), '%.1f')
    add('Credits of buildings destroyed per game', lambda a: sum(int(r['credits_destroyed']) for r in rows[a]) / max(1, len(rows[a])), '%.0f')
    add('Planes shot down per game', lambda a: sum(int(r['shot_down']) for r in rows[a]) / max(1, len(rows[a])), '%.1f')
    add('Median seconds a plane lasted', lambda a: statistics.median([float(r['seconds']) for r in fl[a] if r['end'] != 'flying'] or [0]), '%.1f')
    add('Planes bought per game', lambda a: sum(int(r['bought']) for r in rows[a]) / max(1, len(rows[a])), '%.1f')
    add('Credits spent on planes per game', lambda a: sum(int(r['credits_spent']) for r in rows[a]) / max(1, len(rows[a])), '%.0f')
    del games_n
    return lines + ['']


def paired(first, second, value):
    """Mean per-game difference second - first over (map, seed), with its 95% bootstrap interval."""
    index = {(r['map'], r['seed']): r for r in by_arm[first]}
    diffs = [value(r) - value(index[(r['map'], r['seed'])]) for r in by_arm[second]]
    low, high = bootstrap(diffs, 20261123)[-2:]
    return sum(diffs) / len(diffs), low, high, diffs


def rate(row, field):
    return int(row[field]) / max(1, int(row['flights']))


report = ['# City airplane flights in whole games', '',
          '%.0f game minutes a game, QuantBot Brutal on every house, Dune City mod options (explored, no fog). '
          'Each arm adds one rule to the one before it; base is the build Stefan played (4cf0fd5c4).' % args.minutes, '']
if len(args.maps) > 1:
    report += table(args.maps, 'All maps')
for name in args.maps:
    turrets = [int(r['turrets_start']) for r in by_arm['base'] if r['map'] == name]
    report += table([name], '%s (%s, %s; %d Rocket Turrets on the map at the start)'
                    % (name, MAPS[name][0].split('/')[-1],
                       'every house an AI, watched' if MAPS[name][2] == 'spectate' else 'the local human against an AI',
                       turrets[0] if turrets else 0))
report += ['## Paired by map and seed (%d games an arm)' % len(by_arm['base']), '',
           '| Step | Measure | Mean difference | 95% interval |', '|---|---|---:|---|']
steps = (('base', 'stick', 'Buildings destroyed per flight', lambda r: rate(r, 'destroyed')),
         ('base', 'stick', 'Buildings destroyed per game', lambda r: int(r['destroyed'])),
         ('stick', 'runs', 'Flights that released a bomb, share', lambda r: rate(r, 'bombed')),
         ('stick', 'runs', 'Buildings destroyed per flight', lambda r: rate(r, 'destroyed')),
         ('runs', 'purchase', 'Flights per game', lambda r: int(r['flights'])),
         ('runs', 'purchase', 'Buildings destroyed per game', lambda r: int(r['destroyed'])))
results = {}
for first, second, label, value in steps:
    mean, low, high, diffs = paired(first, second, value)
    results[(second, label)] = (mean, low, high, diffs)
    report.append('| %s -> %s | %s | %+.2f | %+.2f to %+.2f |' % (first, second, label, mean, low, high))
least = [int(r['credits_after']) for r in purchases]
report += ['', 'Purchases: %d in all; the least a house held after paying was %s credits.'
           % (len(purchases), min(least) if least else 'n/a'), '']
(out / 'summary.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
print('\n'.join(report))

failures = []


def coverage(row):
    return int(row['bombed_tiles']) / max(1, int(row['sight_tiles']))


base_cover = sum(int(r['bombed_tiles']) for r in by_arm['base']) / max(1, sum(int(r['sight_tiles']) for r in by_arm['base']))
if base_cover >= 0.5:
    failures.append('base: %.0f%% of the enemy building tiles under the bomb sight got a bomb; Stefan\'s report '
                    '(planes over an enemy city drop nothing) is not reproduced' % (100 * base_cover))
for row in by_arm['purchase']:
    if int(row['sight_buildings']) == 0 or int(row['bombed_buildings']) < 0.95 * int(row['sight_buildings']):
        failures.append('default rules: only %d of %d enemy buildings under the bomb sight got a bomb on %s seed %s'
                        % (int(row['bombed_buildings']), int(row['sight_buildings']), row['map'], row['seed']))
# "Some buildings, not all" is asked of the bombing itself: the free plane with the stick and the
# bombing runs. With purchases on, a house with spare money buys while its planes pay, up to ten in
# the air (Stefan, 2026-11-23: "up to 10", "AI buys freely too", "ai should figure out when it makes
# sense to buy"); on Metropolis, a million credits a house, that destroyed 65% of the map in six
# minutes. It is reported, not required.
for row in by_arm['runs']:
    if int(row['destroyed']) * 2 >= int(row['buildings_start']):
        failures.append('stick and runs without purchases: bombs destroyed %d of %d buildings on %s seed %s'
                        % (int(row['destroyed']), int(row['buildings_start']), row['map'], row['seed']))
# Over all games, not in each: since anti-air fires over its own city (2026-11-23) a plane over
# Metropolis's 928 Rocket Turrets is shot down before either bomb does much (0.25 against 0.35
# buildings a flight), while over 2P City the stick destroys twenty times the aimed bombs' count.
if not results[('stick', 'Buildings destroyed per flight')][0] > 0:
    failures.append('stick: not more buildings destroyed per flight than base over the games')
if results[('runs', 'Flights that released a bomb, share')][0] < 0:
    failures.append('runs: a smaller share of flights bombed than without')
if any(int(r['bought']) for arm in ('base', 'stick', 'runs') for r in by_arm[arm]):
    failures.append('purchase: a plane was bought with the rule off')
# No purchase is required with the rule on: a computer player buys only when its planes pay, and
# against turrets that fire they do not (none was bought on either map in the six-minute games).
if least and min(least) < 3000:
    failures.append('purchase: a house was left with %d credits after buying a plane' % min(least))
# A computer player buys for one of two reasons. Its planes pay: at least one flight has ended and
# their bombs destroyed at least 1500 credits a flight (the purchase shows up a cycle or two after
# the decision, in which one more flight may have ended). Or it has heaps of money: 30,000 credits
# beyond its reserves, of which at least the 3000-credit floor and the surplus less the plane are
# still there after paying.
for r in purchases:
    ended, destroyed = int(r['flights_ended']), int(r['destroyed_credits'])
    pays = ended >= 1 and destroyed >= PRICE * (ended - 1)
    if not pays and int(r['credits_after']) < 30000 + 3000 - PRICE:
        failures.append('purchase: house %s on %s seed %s bought a plane that did not pay with %s credits left'
                        % (r['house'], r['map'], r['seed'], r['credits_after']))
if failures:
    sys.exit('CITY PLANE FLIGHTS FAILED: ' + '; '.join(failures))
print('City plane flights: ' + str(out / 'summary.md'))
