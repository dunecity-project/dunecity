#!/usr/bin/env python3
"""AI scenario matrix (docs/ai-scenario-matrix.md): many kinds of game, one driver, one table.

Every game is a real match played headless to its end by the balance probe (run-campaign-balance.py
--scenario, tests/ai/scenario-match.inc). Categories:

  skirmish  Random Skirmish multiplayer maps, 2-8 houses, free-for-all and teams, starting bases
            MCV only / small / medium / fortress, starting armies none / small / large, vanilla and
            DuneCity, QuantBot difficulties mixed with the AI Player.
  cy        Random Skirmish Construction Yard only starts (each MCV deployed before the first cycle,
            no army, the menu's default 3,000 credits), vanilla and DuneCity.
  aaa       5P - 128x128 - All against Atreides: Stefan's vanilla setup (Atreides QuantBot Brutal
            against four AI Player Hard, his Game Options, the quantbot_all_against_atreides seeds),
            the same in the DuneCity mod, and the reverse sanity check (Atreides QuantBot Medium
            against four QuantBot Hard must lose).
  campaign  Atreides, Harkonnen and Ordos missions of levels 1-9 (the level's first mission, and the
            second for levels 2-8), vanilla and DuneCity: the human seat's QuantBot partner against the
            campaign's default enemy AI (QuantBot Easy, as the campaign menu starts it; one cell with
            the "scenario settings" CampaignAIPlayer). Hard must win; Easy and Medium are reported.
  ladder    QuantBot against QuantBot 1v1, every adjacent pair (Brutal v Hard, Hard v Medium, Medium v
            Easy) plus Brutal v Medium and Hard v Easy, on a vanilla and a DuneCity Random Skirmish map,
            Habbanya-Penny and a Construction Yard only start. Full tier: every seed twice, the houses
            swapped (the same starts), so neither the house nor the start decides the ladder: the
            stronger difficulty must win.
  mp        The most-played bundled multiplayer maps (dunelegacy.com usage statistics, 2026-10-18)
            1v1, 2v2 and 3v3: QuantBot Hard teams against QuantBot Hard teams, and against AI
            Player Hard teams (which they must beat).

Tiers: --tier smoke (one fast representative per category with pass/fail checks, plus a replay
of one game that must reproduce its final state digest; ctest scenario_matrix_smoke) or --tier full
(the whole matrix, enough games per cell for intervals). --shard K/N plays every N-th game, so a
full run splits across machines; --report reads one or more results directories and prints the
summary table. Games already in games.csv are skipped; each game's run directory is deleted as
soon as its rows are written. Scores are tests/ai/match_score.py placement scores.
"""
import argparse
import csv
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402
import match_score  # noqa: E402

MP = root / 'data/maps/multiplayer'
AAA_MAP = root / 'data/maps/singleplayer/5P - 128x128 - All against Atreides.ini'
AAA_OPTIONS = root / 'tests/ai/all-against-atreides-options.ini'
# quantbot_all_against_atreides's seeds (run-all-against-atreides.py DEFAULT_SEEDS).
AAA_SEEDS = (1756374161, 906213928, 41562270, 1841712204, 1430976924, 1804669015)
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5,
                 'neutral': 6, 'rebels': 7}
# Houses for N players, in this order (the 7th and 8th custom-game houses last).
HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary', 'rebels', 'neutral')
CLASSES = {'QE': 'qBotEasy', 'QM': 'qBotMedium', 'QH': 'qBotHard', 'QB': 'qBotBrutal',
           'AE': 'AIPlayerEasy', 'AM': 'AIPlayerMedium', 'AH': 'AIPlayerHard'}
QUANTBOT_ORDER = ('QB', 'QH', 'QM', 'QE')
DIFFICULTY = {'QE': 'easy', 'QM': 'medium', 'QH': 'hard', 'QB': 'brutal'}
CATEGORIES = ('skirmish', 'cy', 'aaa', 'campaign', 'ladder', 'mp')
GAME_FIELDS = ('id', 'category', 'cell', 'seed', 'mod', 'map', 'sides', 'expect', 'outcome', 'winner_team',
               'winner', 'end_s', 'minutes', 'limit', 'eliminations', 'triggers', 'fired', 'win_flags', 'quota',
               'city_pop', 'win_type', 'digest', 'wall_s', 'error')
HOUSE_FIELDS = ('id', 'category', 'cell', 'house', 'team', 'code', 'players', 'alive', 'eliminated_s',
                'kill_value', 'loss_value', 'structures', 'army_value', 'army_units', 'away_peak',
                'sortie_minutes', 'place', 'score', 'drawn', 'start')
# A house is idle when it survives (or lasts) this many game minutes without a sortie or a kill.
IDLE_MINUTES = 20
# Full-tier seeds per ladder cell. Each seed is played twice with the houses swapped, so 18 seeds
# on the four maps give 144 games per pair (--ladder-seeds 9: 72).
LADDER_SEEDS = 18
lock = threading.Lock()


# ---- The matrix -------------------------------------------------------------------------

def mp_map(name):
    path = MP / f'{name}.ini'
    if not path.is_file():
        raise SystemExit(f'Missing bundled map {path}')
    return path


def match(category, name, mod, slots, *, skirmish=None, path=None, houses=None, minutes=60, options=None,
          expect=None, smoke=0, full=4, rotate=True, repeat=False, both_sides=False):
    """A match cell. slots: [(class code, team)] in slot order; houses: fixed houses (map-named),
    else the first len(slots) of HOUSES, rotated by the seed's index so every code plays every house.
    expect: ('wins', team[, within minutes]) or ('loses', team): that team must win / another must.
    both_sides (full tier): every seed is played a second time ('#swap') with the houses rotated by
    one more; the slots keep their order, so in a 1v1 each side keeps its start and swaps its house."""
    return dict(category=category, name=name, kind='match', mod=mod, slots=slots, skirmish=skirmish, path=path,
                houses=houses, minutes=minutes, options=options, expect=expect, smoke=smoke, full=full,
                rotate=rotate, repeat=repeat, both_sides=both_sides)


def team_slots(*teams):
    """team_slots(['QH', 'QE'], ['QM', 'QM']) -> [(QH, 1), (QE, 1), (QM, 2), (QM, 2)]"""
    return [(code, index) for index, team in enumerate(teams, 1) for code in team]


def ffa(*codes):
    return [(code, index) for index, code in enumerate(codes, 1)]


def skirmish_spec(size, base, army, **extra):
    return ','.join([f'size={size}', f'base={base}', f'army={army}'] + [f'{k}={v}' for k, v in extra.items()])


def matrix(ladder_seeds=LADDER_SEEDS):
    cells = []
    # 1. Random Skirmish, mixed: players, teams, bases, armies (kArmyBudgets index: 0 none, 2 = 2,500
    # small, 5 = 20,000 large), mods, QuantBot difficulties and the AI Player.
    s = lambda name, mod, slots, spec, **kw: cells.append(match('skirmish', name, mod, slots, skirmish=spec, **kw))
    s('2p-small-smallarmy', 'vanilla', ffa('QH', 'AH'), skirmish_spec(64, 'small', 2), expect=('wins', 1))
    s('2p-medium-noarmy', 'dunecity', ffa('QB', 'QM'), skirmish_spec(96, 'medium', 0), expect=('wins', 1))
    s('3p-ffa-mcv-smallarmy', 'vanilla', ffa('QH', 'QM', 'AH'), skirmish_spec(128, 'none', 2))
    s('4p-ffa-medium-smallarmy', 'vanilla', ffa('QB', 'QH', 'QM', 'QE'), skirmish_spec(128, 'medium', 2), smoke=1)
    s('4p-ffa-small-largearmy', 'dunecity', ffa('QH', 'QM', 'AH', 'AM'), skirmish_spec(128, 'small', 5))
    s('2v2-fortress-largearmy', 'vanilla', team_slots(['QH', 'QH'], ['QE', 'QE']), skirmish_spec(128, 'fortress', 5),
      expect=('wins', 1))
    s('2v2-medium-noarmy', 'dunecity', team_slots(['QH', 'AH'], ['QM', 'AM']), skirmish_spec(128, 'medium', 0),
      expect=('wins', 1))
    s('5p-ffa-small-noarmy', 'vanilla', ffa('QB', 'QH', 'QM', 'QE', 'AH'), skirmish_spec(160, 'small', 0))
    s('3v3-medium-smallarmy', 'vanilla', team_slots(['QH', 'QH', 'QH'], ['QM', 'QM', 'QM']),
      skirmish_spec(160, 'medium', 2), expect=('wins', 1))
    s('6p-ffa-small-smallarmy', 'dunecity', ffa('QB', 'QH', 'QM', 'QE', 'AH', 'AM'), skirmish_spec(160, 'small', 2))
    s('7p-ffa-mcv-noarmy', 'vanilla', ffa('QH', 'QH', 'QM', 'QM', 'QE', 'AH', 'AM'), skirmish_spec(192, 'none', 0),
      minutes=80)
    s('8p-ffa-medium-smallarmy', 'vanilla', ffa('QB', 'QH', 'QH', 'QM', 'QM', 'QE', 'AH', 'AM'),
      skirmish_spec(192, 'medium', 2), minutes=80)
    s('4v4-fortress-largearmy', 'dunecity', team_slots(['QH'] * 4, ['QM'] * 4), skirmish_spec(192, 'fortress', 5),
      expect=('wins', 1), minutes=80)
    # 2. Construction Yard only, default credits.
    c = lambda name, mod, slots, size, **kw: cells.append(
        match('cy', name, mod, slots, skirmish=skirmish_spec(size, 'cy', 0), **kw))
    c('1v1-qh-v-ah', 'vanilla', ffa('QH', 'AH'), 96, expect=('wins', 1))
    c('1v1-qh-v-qm', 'dunecity', ffa('QH', 'QM'), 96, expect=('wins', 1))
    c('1v1-qb-v-qe', 'dunecity', ffa('QB', 'QE'), 64, expect=('wins', 1, 40), smoke=1, repeat=True)
    c('1v1-qh-mirror', 'vanilla', ffa('QH', 'QH'), 96)
    c('4p-ffa', 'vanilla', ffa('QH', 'QM', 'QE', 'AH'), 128)
    c('4p-ffa', 'dunecity', ffa('QB', 'QH', 'QM', 'QE'), 128)
    c('2v2-mirror', 'dunecity', team_slots(['QH', 'QM'], ['QH', 'QM']), 128)
    # 3. All against Atreides (map-named houses; Atreides alone on team 2 as in Stefan's lobby).
    aaa_houses = ['harkonnen', 'atreides', 'ordos', 'fremen', 'sardaukar']
    aaa = lambda name, mod, codes, **kw: cells.append(match(
        'aaa', name, mod, [(code, 2 if house == 'atreides' else 1) for house, code in zip(aaa_houses, codes)],
        path=AAA_MAP, houses=aaa_houses, rotate=False, **kw))
    aaa('qb-v-4ah', 'vanilla', ['AH', 'QB', 'AH', 'AH', 'AH'], options=AAA_OPTIONS, minutes=41,
        expect=('wins', 2, 40), full=6)
    # Stefan's 40-minute rule is for his vanilla setup; in DuneCity Atreides must win within the cap.
    aaa('qb-v-4ah', 'dunecity', ['AH', 'QB', 'AH', 'AH', 'AH'], options=AAA_OPTIONS, minutes=60,
        expect=('wins', 2), full=6)
    aaa('reverse-qm-v-4qh', 'vanilla', ['QH', 'QM', 'QH', 'QH', 'QH'], options=AAA_OPTIONS, minutes=60,
        expect=('loses', 2), smoke=1, full=6)
    # 4. Campaign (built per mission in schedule()).
    for mod in ('vanilla', 'dunecity'):
        for partner in ('hard', 'medium', 'easy'):
            cells.append(dict(category='campaign', name=f'partner-{partner}', kind='campaign', mod=mod,
                              partner=partner, enemy_ai='quantbot', minutes=80, full=2 if partner == 'hard' else 1,
                              smoke=0))
    cells.append(dict(category='campaign', name='partner-hard-v-campaign-ai', kind='campaign', mod='vanilla',
                      partner='hard', enemy_ai='campaign', minutes=80, full=1, smoke=0, first_only=True))
    # 5. Difficulty ladder: the stronger QuantBot must win. Every adjacent pair, plus Brutal v Medium and
    # Hard v Easy; three Random Skirmish/bundled maps and a Construction Yard only start; in the full tier
    # both houses of every seed (each side keeps its start); --ladder-seeds plays more seeds per cell.
    ladder_maps = (('rs-vanilla', 'vanilla', dict(skirmish=skirmish_spec(96, 'small', 2))),
                   ('rs-dunecity', 'dunecity', dict(skirmish=skirmish_spec(96, 'small', 2))),
                   ('habbanya-penny', 'vanilla', dict(path=mp_map('2P - 128x128 - 1v1 - Habbanya-Penny'))),
                   ('cy', 'vanilla', dict(skirmish=skirmish_spec(96, 'cy', 0))))
    for strong, weak in (('QB', 'QH'), ('QH', 'QM'), ('QM', 'QE'), ('QB', 'QM'), ('QH', 'QE')):
        for map_name, mod, where in ladder_maps:
            smoke = 2 if (strong, weak, map_name) == ('QH', 'QE', 'rs-vanilla') else 0
            cells.append(match('ladder', f'{strong}-v-{weak}-{map_name}', mod, ffa(strong, weak), expect=('wins', 1),
                               smoke=smoke, full=ladder_seeds, both_sides=True, **where))
    # 6. Most-played bundled multiplayer maps (dunelegacy.com/usage.html, all time, 2026-10-18).
    for name, players in (('2P - 128x128 - 1v1 - Habbanya-Penny', 1), ('2P - 128x128 - 1v1 - Tuono-Orac', 1),
                          ('2P - 128x128 - 1v1 - Sihaya-Chvrches', 1), ('4P - 128x64 - 2v2 - Sihaya-Toto', 2),
                          ('4P - 128x128 - Deserted', 2), ('4P - 192x192 - DuneCity', 2),
                          ('6P - 128x128 - 3v3 - Sihaya-Lenka', 3), ('6P - 128x128 - Full Wormage', 3),
                          ('6P - 256x256 - Alkozeltser 4', 3)):
        mod = 'dunecity' if name.endswith('DuneCity') else 'vanilla'
        named = None
        if players == 3:   # every 6P map names Fremen (Sihaya-Lenka all six houses)
            named = ['harkonnen', 'atreides', 'ordos', 'fremen', 'sardaukar', 'mercenary']
        short = name.rsplit(' - ', 1)[-1].lower().replace(' ', '-')
        size = f'{players}v{players}'
        big = '256x256' in name
        cells.append(match('mp', f'{size}-{short}-qh-mirror', mod, team_slots(['QH'] * players, ['QH'] * players),
                           path=mp_map(name), houses=named, minutes=80 if big else 60, full=2 if big else 4,
                           smoke=1 if short == 'sihaya-toto' else 0))
        if players > 1 and not big:
            cells.append(match('mp', f'{size}-{short}-qh-v-ah', mod, team_slots(['QH'] * players, ['AH'] * players),
                               path=mp_map(name), houses=named, expect=('wins', 1), full=4))
    return cells


def campaign_missions(first_only):
    """(level, mission) per level: the first, and for levels 2-8 the second (CampaignControls)."""
    out = []
    for level in range(1, 10):
        first = 1 if level == 1 else 22 if level == 9 else (level - 1) * 3 - 1
        out.append((level, first))
        if 2 <= level <= 8 and not first_only:
            out.append((level, first + 1))
    return out


def seeds_for(category, cell_name, count):
    base = {'skirmish': 5101, 'cy': 6203, 'ladder': 7307, 'mp': 8411, 'campaign': 42}[category]
    offset = sum(ord(ch) for ch in cell_name) % 97
    return [base + 1009 * (i + offset) for i in range(count)]


def schedule(args):
    games = []
    for cell in matrix(getattr(args, 'ladder_seeds', LADDER_SEEDS)):
        if args.categories and cell['category'] not in args.categories:
            continue
        if getattr(args, 'cells', None) and not re.search(args.cells, f'{cell["category"]}/{cell["name"]}/{cell["mod"]}'):
            continue
        count = cell['smoke'] if args.tier == 'smoke' else cell['full']
        if args.seeds_per_cell is not None and args.tier == 'full':
            count = min(count, args.seeds_per_cell)
        if count <= 0:
            continue
        if cell['kind'] == 'campaign':
            for house in ('atreides', 'harkonnen', 'ordos'):
                for level, mission in campaign_missions(cell.get('first_only', False)):
                    for seed in seeds_for('campaign', f'{house}{mission}', count):
                        games.append(campaign_game(cell, house, level, mission, seed))
            continue
        seeds = list(AAA_SEEDS[:count]) if cell['category'] == 'aaa' else seeds_for(cell['category'], cell['name'], count)
        for index, seed in enumerate(seeds):
            games.append(match_game(cell, index, seed))
            if cell['repeat'] and index == 0 and args.tier == 'smoke':
                games.append(dict(match_game(cell, index, seed), id=match_game(cell, index, seed)['id'] + '#replay'))
            if cell['both_sides'] and args.tier == 'full':
                games.append(match_game(cell, index + 1, seed, '#swap'))
    for cell in smoke_campaigns() if args.tier == 'smoke' and (not args.categories or 'campaign' in args.categories) else ():
        games.append(cell)
    if args.shard:
        k, n = args.shard
        games = [g for i, g in enumerate(games) if i % n == k - 1]
    return games


def smoke_campaigns():
    """Smoke: two quick campaign missions and a level 9 one, the partner at Hard."""
    cell = dict(category='campaign', name='partner-hard', kind='campaign', partner='hard', enemy_ai='quantbot',
                minutes=60)
    return [campaign_game(dict(cell, mod='vanilla'), 'atreides', 1, 1, 42),
            campaign_game(dict(cell, mod='vanilla'), 'ordos', 4, 5, 42),
            campaign_game(dict(cell, mod='dunecity'), 'harkonnen', 9, 22, 42)]


def match_game(cell, index, seed, suffix=''):
    n = len(cell['slots'])
    houses = list(cell['houses'] or HOUSES[:n])
    if cell['rotate']:
        shift = index % n
        houses = houses[shift:] + houses[:shift]
    sides = [(house, team, code) for house, (code, team) in zip(houses, cell['slots'])]
    where = f'skirmish:{cell["skirmish"]},seed={seed}' if cell['skirmish'] else Path(cell['path']).stem
    return dict(id=f'{cell["category"]}/{cell["name"]}/{cell["mod"]}/{seed}{suffix}', category=cell['category'],
                cell=f'{cell["name"]}/{cell["mod"]}', kind='match', seed=seed, mod=cell['mod'], map=where,
                sides=sides, minutes=cell['minutes'], options=cell['options'], expect=cell['expect'],
                skirmish=f'{cell["skirmish"]},seed={seed}' if cell['skirmish'] else None, path=cell['path'])


def campaign_game(cell, house, level, mission, seed):
    enemy = 'campaign AI' if cell['enemy_ai'] == 'campaign' else 'QE'
    return dict(id=f'campaign/{cell["name"]}/{cell["mod"]}/{house}-m{mission}/{seed}', category='campaign',
                cell=f'{cell["name"]}/{cell["mod"]}', kind='campaign', seed=seed, mod=cell['mod'],
                map=f'{house} level {level} mission {mission}', house=house, level=level, mission=mission,
                partner=cell['partner'], enemy_ai=cell['enemy_ai'], minutes=cell['minutes'],
                sides=[(house, 1, 'Q' + cell['partner'][0].upper()), ('enemies', 2, enemy)],
                expect=('campaign_won',) if cell['partner'] == 'hard' else None)


# ---- Playing ----------------------------------------------------------------------------

def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    with lock:
        new = not path.exists()
        if not new:
            # A results directory from before a column change (e.g. the retired economic_threshold)
            # is rewritten with the current columns, so appended rows stay aligned.
            old = read(path)
            with path.open(newline='') as handle:
                header = next(csv.reader(handle), [])
            if tuple(header) != tuple(fields):
                with path.open('w', newline='') as handle:
                    writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
                    writer.writeheader()
                    writer.writerows(old)
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def command(args, binary, game, out):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--scenario', '--mod', game['mod'],
           '--seed', str(game['seed']), '--minutes', str(game['minutes']), '--capture-mib', '256',
           '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--no-ai-telemetry']
    if game['kind'] == 'campaign':
        cmd += ['--house', game['house'], '--level', str(game['level']), '--mission', str(game['mission']),
                '--partner-difficulty', game['partner'], '--enemy-difficulty', 'easy', '--enemy-ai', game['enemy_ai']]
        return cmd
    sides = ','.join(f'{house}:{team}:{CLASSES[code]}' for house, team, code in game['sides'])
    cmd += ['--house', game['sides'][0][0], '--scenario-sides', sides, '--local-spectator']
    cmd += ['--skirmish', game['skirmish']] if game['skirmish'] else ['--custom-map', str(game['path'])]
    if game['options']:
        cmd += ['--game-options', str(game['options'])]
    return cmd


def win_type(result, houses, campaign):
    if not result or result.get('outcome') != 'won' or not campaign:
        return ''
    if int(result.get('quota') or 0) > 0 and int(result.get('win_flags') or 0) & 4 and \
            any(h['alive'] == '1' for h in houses if h['team'] != '1'):
        return 'quota'
    return 'conquest'


def play(args, scratch, binary, game):
    out = scratch / f'g{abs(hash(game["id"])) % 10**10}'
    shutil.rmtree(out, ignore_errors=True)
    started = time.time()
    outcome = subprocess.run(command(args, binary, game, out), cwd=root, capture_output=True, text=True)
    wall = round(time.time() - started)
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((parse(l, 'SCENARIO_RESULT:') for l in lines if 'SCENARIO_RESULT:' in l), {})
    houses = [parse(l, 'SCENARIO_HOUSE:') for l in lines if 'SCENARIO_HOUSE:' in l]
    digest = next((parse(l, 'PROBE_DIGEST:').get('digest', '') for l in lines if 'PROBE_DIGEST:' in l and 'final=1' in l), '')
    campaign = game['kind'] == 'campaign'
    code_of = {HOUSE_NUMBERS[h]: code for h, _, code in game['sides'] if h in HOUSE_NUMBERS}
    row = {'id': game['id'], 'category': game['category'], 'cell': game['cell'], 'seed': game['seed'],
           'mod': game['mod'], 'map': game['map'], 'limit': game['minutes'],
           'sides': ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']),
           'expect': ':'.join(str(x) for x in game['expect']) if game['expect'] else '',
           'outcome': result.get('outcome', ''), 'winner_team': result.get('winner_team', ''),
           'end_s': result.get('end_s', ''), 'minutes': result.get('minutes', ''),
           'eliminations': sum(1 for h in houses if h.get('alive') == '0'),
           'triggers': result.get('triggers', ''), 'fired': result.get('fired', ''),
           'win_flags': result.get('win_flags', ''), 'quota': result.get('quota', ''),
           'city_pop': result.get('city_pop', ''),
           'win_type': win_type(result, houses, campaign), 'digest': digest, 'wall_s': wall,
           'error': '' if outcome.returncode == 0 and result else f'rc={outcome.returncode}'}
    winner = result.get('winner_team', '')
    row['winner'] = '+'.join(sorted({code_of.get(int(h['house']), '?') for h in houses if h['team'] == winner})) \
        if winner not in ('', '0', '-1') and not campaign else ''
    results = Path(args.results)
    if row['error']:
        (results / f'failed-{out.name}.txt').write_text(game['id'] + '\n' + outcome.stdout[-4000:] + '\n'
                                                       + outcome.stderr[-4000:] + '\n' + '\n'.join(lines[-80:]))
        append(results / 'games.csv', GAME_FIELDS, [row])
        finish_game(scratch, out, True)
        return row
    # Placement scores (match_score): teams are entrants; in a campaign only the partner's side counts.
    ids = [int(h['house']) for h in houses]
    eliminated = {int(h['house']): float(h['eliminated_s']) for h in houses if h['alive'] == '0'}
    teams = {int(h['house']): int(h['team']) for h in houses}
    placing = match_score.score_game(ids, eliminated, args.draw_score, teams) if len(set(teams.values())) > 1 else {}
    house_rows = []
    for h in houses:
        number = int(h['house'])
        p = placing.get(number)
        house_rows.append({'id': game['id'], 'category': game['category'], 'cell': game['cell'],
                           'house': number, 'team': h['team'], 'code': code_of.get(number, 'enemy' if campaign else '?'),
                           'players': h['players'], 'alive': h['alive'], 'eliminated_s': h['eliminated_s'],
                           'kill_value': h['kill_value'], 'loss_value': h['loss_value'], 'structures': h['structures'],
                           'army_value': h['army_value'], 'army_units': h['army_units'], 'away_peak': h['away_peak'],
                           'sortie_minutes': h['sortie_minutes'],
                           'place': p.place if p else '', 'score': round(p.score, 4) if p else '',
                           'drawn': int(p.drawn) if p else '', 'start': h.get('start', '')})
    append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, False)
    return row


def build_probe(args, scratch):
    probe = scratch / 'probe'
    shutil.rmtree(probe, ignore_errors=True)
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
                    '--output-dir', str(probe), '--scenario', '--skirmish', skirmish_spec(64, 'cy', 0),
                    '--scenario-sides', 'harkonnen:1:qBotEasy,atreides:2:qBotEasy', '--house', 'harkonnen',
                    '--local-spectator', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(probe / 'profile', ignore_errors=True)
    return probe_binary(probe)


def run(args):
    results = Path(args.results)
    if args.fresh:
        shutil.rmtree(results, ignore_errors=True)
    results.mkdir(parents=True, exist_ok=True)
    done = {r['id'] for r in read(results / 'games.csv') if not r['error']}
    if (results / 'games.csv').exists():
        # Failed games are retried: keep only the successful rows.
        kept = [r for r in read(results / 'games.csv') if not r['error']]
        (results / 'games.csv').unlink()
        append(results / 'games.csv', GAME_FIELDS, kept)
    todo = [g for g in schedule(args) if g['id'] not in done]
    print(f'{len(todo)} games to play ({len(done)} already in games.csv)', flush=True)
    if todo:
        require_free_space()
        with scratch_dir('scenario-matrix', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            started = time.time()
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, g) for g in todo]
                for count, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    print(f'[{count}/{len(todo)} {round(time.time() - started)} s] {row["id"]} -> '
                          f'{row["outcome"] or "?"} {row["winner"]} ({row["minutes"]} min, {row["wall_s"]} s wall)'
                          f'{" " + row["error"] if row["error"] else ""}', flush=True)
    if args.tier == 'smoke':
        sys.exit(0 if smoke_checks(args, results) else 1)


# ---- Checks and report ------------------------------------------------------------------

def expectation(game_row):
    """True / False if the game has an expected result, None otherwise."""
    expect = game_row['expect'].split(':') if game_row['expect'] else []
    if not expect or game_row['error']:
        return None
    if expect[0] == 'campaign_won':
        return game_row['outcome'] == 'won'
    team = expect[1]
    if expect[0] == 'wins':
        within = float(expect[2]) if len(expect) > 2 else None
        return game_row['winner_team'] == team and (within is None or float(game_row['minutes']) <= within)
    if expect[0] == 'loses':
        return game_row['winner_team'] not in ('', '0', '-1', team)
    raise ValueError(game_row['expect'])


def flags(game, houses):
    """Problems of one game: crash, time-out, no elimination, idle houses, campaign not finished."""
    out = []
    if game['error']:
        return [f'crash ({game["error"]})']
    minutes = float(game['minutes'] or 0)
    if game['category'] == 'campaign':
        if game['outcome'] == 'time_limit':
            out.append('campaign not finished')
        elif game['outcome'] == 'lost':
            out.append('campaign lost')
        partner = [h for h in houses if h['team'] == '1']
        if game['outcome'] != 'won' and minutes >= IDLE_MINUTES and partner and all(
                h['sortie_minutes'] == '0' and h['kill_value'] == '0' for h in partner):
            out.append('partner idle')
        return out
    if game['winner_team'] == '0':
        out.append('time-out')
    if len(houses) > 2 and game['eliminations'] == '0':
        out.append('nobody eliminated')
    for h in houses:
        lasted = minutes if h['alive'] == '1' else float(h['eliminated_s']) / 60
        if lasted >= IDLE_MINUTES and h['sortie_minutes'] == '0' and h['kill_value'] == '0':
            out.append(f'{h["code"]} idle')
    return out


def wilson(k, n):
    if n == 0:
        return (0.0, 0.0)
    z = 1.96
    p = k / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * ((p * (1 - p) / n + z * z / (4 * n * n)) ** 0.5) / (1 + z * z / n)
    return (max(0.0, centre - half), min(1.0, centre + half))


def load(dirs):
    games, houses = {}, defaultdict(list)
    for d in dirs:
        for row in read(Path(d) / 'games.csv'):
            if row['id'] not in games or games[row['id']]['error']:
                games[row['id']] = row
        for row in read(Path(d) / 'houses.csv'):
            houses[row['id']].append(row)
    # A replayed game appears once per run; houses rows of the latest run only.
    for key, rows in houses.items():
        seen = {}
        for row in rows:
            seen[row['house']] = row
        houses[key] = list(seen.values())
    # Expectations as the matrix defines them now (a row keeps the one it was played with).
    current = {}
    for tier in ('smoke', 'full'):
        plan = argparse.Namespace(tier=tier, categories=None, seeds_per_cell=None, shard=None, ladder_seeds=LADDER_SEEDS)
        current.update({g['id']: ':'.join(str(x) for x in g['expect']) if g['expect'] else '' for g in schedule(plan)})
    for gid, row in games.items():
        row['expect'] = current.get(gid, row['expect'])
    return games, houses


def smoke_checks(args, results):
    games, houses = load([results])
    ok = True
    print('\nScenario matrix smoke checks:')
    for gid, game in sorted(games.items()):
        hs = houses.get(gid, [])
        problems = []
        if game['error']:
            problems.append(f'crash ({game["error"]})')
        else:
            if expectation(game) is False:
                problems.append(f'expected {game["expect"]}, got winner team {game["winner_team"]} '
                                f'({game["outcome"]}, {game["minutes"]} min)')
            if game['category'] == 'campaign':
                if game['outcome'] != 'won' and float(game['minutes']) < 5:
                    problems.append('campaign ended before 5 minutes without a win')
            else:
                if game['winner_team'] == '0':
                    problems.append('nobody won (time-out)')
                if len(hs) > 2 and game['eliminations'] == '0':
                    problems.append('nobody eliminated')
                sortied = sum(1 for h in hs if h['sortie_minutes'] != '0' or h['kill_value'] != '0')
                if sortied < 2:
                    problems.append(f'only {sortied} house(s) left base or killed anything')
        if gid.endswith('#replay'):
            original = games.get(gid[:-len('#replay')])
            if original and original['digest'] != game['digest']:
                problems.append(f'replay diverged: final digest {game["digest"]} != {original["digest"]}')
        ok &= not problems
        print(f'  {"PASS" if not problems else "FAIL"} {gid}: {game["outcome"]} {game["winner"]} '
              f'{game["minutes"]} min{"; " + "; ".join(problems) if problems else ""}')
    expected = len(schedule(args))
    if len(games) != expected:
        print(f'  FAIL: {len(games)} games recorded, {expected} scheduled')
        ok = False
    print('scenario matrix smoke: ' + ('PASS' if ok else 'FAIL'))
    return ok


def fmt_ci(values, seed=1):
    if not values:
        return '-'
    low, high = match_score.bootstrap(values, seed)
    return f'{statistics.mean(values):+.2f} [{low:+.2f}, {high:+.2f}]'


def report(args):
    games, houses = load(args.report_dirs)
    if not games:
        raise SystemExit('No games in ' + ', '.join(args.report_dirs))
    print(f'# AI scenario matrix: {len(games)} games\n')
    print(match_score.describe('placement', args.draw_score, 0) + '\n')
    problems = []
    for category in CATEGORIES:
        rows = [g for g in games.values() if g['category'] == category]
        if not rows:
            continue
        print(f'## {category}\n')
        if category == 'campaign':
            campaign_report(rows, houses, problems)
            continue
        print('| Cell | Games | Decided | Winners (team codes) | Mean min | Expected | Check | Flags |')
        print('|---|---|---|---|---|---|---|---|')
        by_cell = defaultdict(list)
        for g in rows:
            by_cell[g['cell']].append(g)
        for cell, gs in by_cell.items():
            ok = [g for g in gs if not g['error']]
            decided = [g for g in ok if g['winner_team'] not in ('', '0', '-1')]
            winners = defaultdict(int)
            for g in decided:
                winners[f'T{g["winner_team"]} {g["winner"]}'] += 1
            checks = [expectation(g) for g in ok]
            checks = [c for c in checks if c is not None]
            expect = gs[0]['expect']
            if checks:
                k, n = sum(checks), len(checks)
                low, high = wilson(k, n)
                check = f'{k}/{n} [{low:.2f}-{high:.2f}]'
                if high < 0.5:
                    problems.append(f'{category} {cell}: expectation {expect} inverted ({k}/{n})')
                elif k < n:
                    problems.append(f'{category} {cell}: expectation {expect} missed in {n - k}/{n}')
            else:
                check = '-'
            game_flags = defaultdict(int)
            for g in gs:
                for f in flags(g, houses.get(g['id'], [])):
                    game_flags[f] += 1
            if len(decided) == 0 and ok:
                problems.append(f'{category} {cell}: every game a time-out')
            for f, count in game_flags.items():
                if f.startswith('crash') or f.endswith('idle'):
                    problems.append(f'{category} {cell}: {f} in {count}/{len(gs)}')
            mean_min = statistics.mean(float(g['minutes']) for g in ok) if ok else 0
            print(f'| {cell} | {len(gs)} | {len(decided)}/{len(ok)} | '
                  f'{", ".join(f"{w} {c}" for w, c in sorted(winners.items())) or "-"} | {mean_min:.1f} | '
                  f'{expect.replace(":", " ") or "-"} | {check} | '
                  f'{", ".join(f"{f} {c}" for f, c in sorted(game_flags.items())) or "-"} |')
        print()
        if category in ('skirmish', 'cy', 'mp'):
            class_table(rows, houses)
        if category == 'ladder':
            ladder_table(rows, houses, problems)
    print('## Problems\n')
    for p in problems or ['none']:
        print(f'- {p}')


def class_table(rows, houses):
    scores = defaultdict(list)
    for g in rows:
        if g['error']:
            continue
        for h in houses.get(g['id'], []):
            if h['score'] != '':
                scores[h['code']].append(float(h['score']))
    print('Placement score by controller (all games of the category; teams are entrants):\n')
    print('| Controller | Houses | Mean score [95% CI] |')
    print('|---|---|---|')
    for code in sorted(scores, key=lambda c: -statistics.mean(scores[c])):
        print(f'| {code} ({CLASSES.get(code, code)}) | {len(scores[code])} | {fmt_ci(scores[code])} |')
    means = {c: statistics.mean(v) for c, v in scores.items() if c in QUANTBOT_ORDER}
    order = [c for c in QUANTBOT_ORDER if c in means]
    holds = all(means[a] >= means[b] for a, b in zip(order, order[1:]))
    print(f'\nQuantBot order {" > ".join(order)} by mean score: {"holds" if holds else "INVERTED"}\n')


def ladder_table(rows, houses, problems):
    print('Ladder (stronger side\'s placement score; the order holds when the interval is above 0). The '
          'interval resamples seeds: the two games of a seed (houses swapped) count as one mean.\n')
    print('| Pair | Map | Games | Strong W-L-T | Strong score [95% CI] | Order | Strong wins as Harkonnen / Atreides |')
    print('|---|---|---|---|---|---|---|')
    by_pair = defaultdict(list)
    for g in rows:
        if g['error']:
            continue
        name = g['cell'].split('/')[0]
        strong, _, weak, map_name = name.split('-', 3)
        by_pair[(f'{strong} v {weak}', map_name)].append(g)
        by_pair[(f'{strong} v {weak}', 'all maps')].append(g)
    for (pair, map_name), gs in sorted(by_pair.items()):
        strong = pair.split()[0]
        per_seed = defaultdict(list)
        as_house = defaultdict(lambda: [0, 0])
        for g in gs:
            for h in houses.get(g['id'], []):
                if h['code'] != strong:
                    continue
                per_seed[(g['cell'], g['seed'])].append(float(h['score']))
                house = next((key for key, number in HOUSE_NUMBERS.items() if str(number) == h['house']), h['house'])
                as_house[house][1] += 1
                as_house[house][0] += g['winner_team'] == '1'
        scores = [statistics.mean(s) for s in per_seed.values()]
        w = sum(1 for g in gs if g['winner_team'] == '1')
        t = sum(1 for g in gs if g['winner_team'] == '0')
        low, _ = match_score.bootstrap(scores, 1) if scores else (0, 0)
        holds = 'holds' if low > 0 else ('INVERTED' if statistics.mean(scores) < 0 else 'not shown') if scores else '-'
        if map_name == 'all maps' and holds != 'holds':
            problems.append(f'ladder {pair}: order {holds} (strong score {fmt_ci(scores)})')
        sides = ' / '.join(f'{as_house[h][0]}/{as_house[h][1]}' for h in ('harkonnen', 'atreides'))
        print(f'| {pair} | {map_name} | {len(gs)} | {w}-{len(gs) - w - t}-{t} | {fmt_ci(scores)} | {holds} | {sides} |')
    print()


def campaign_report(rows, houses, problems):
    print('| Mod | Partner | Enemy | Missions | Won | Lost | Not finished | Crash | Win by conquest/quota '
          '| Mean min (won) | Triggers fired |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    by_cell = defaultdict(list)
    for g in rows:
        by_cell[g['cell']].append(g)
    for cell, gs in sorted(by_cell.items()):
        name, mod = cell.split('/')
        partner = name.split('-')[1]
        enemy = 'CampaignAIPlayer' if 'campaign-ai' in name else 'QuantBot Easy'
        ok = [g for g in gs if not g['error']]
        won = [g for g in ok if g['outcome'] == 'won']
        types = defaultdict(int)
        for g in won:
            types[g['win_type']] += 1
        fired = sum(int(g['fired'] or 0) for g in ok)
        mean_won = statistics.mean(float(g['minutes']) for g in won) if won else 0
        print(f'| {mod} | {partner} | {enemy} | {len(gs)} | {len(won)} | {sum(g["outcome"] == "lost" for g in ok)} | '
              f'{sum(g["outcome"] == "time_limit" for g in ok)} | {len(gs) - len(ok)} | '
              f'{types["conquest"]}/{types["quota"]} | {mean_won:.1f} | {fired} |')
        for g in gs:
            if g['error']:
                problems.append(f'campaign {g["id"]}: crash ({g["error"]})')
            elif partner == 'hard' and g['outcome'] != 'won':
                problems.append(f'campaign {g["id"]}: Hard partner {g["outcome"]} at {g["minutes"]} min')
    print()
    # Per mission (Hard partner): which missions do not end in a win.
    print('Hard partner per mission (W won, L lost, T not finished, X crash; q = quota win):\n')
    grid = defaultdict(str)
    for g in rows:
        name, mod = g['cell'].split('/')
        if name != 'partner-hard':
            continue
        house, mission = g['id'].split('/')[3].split('-m')
        mark = 'X' if g['error'] else {'won': 'W', 'lost': 'L', 'time_limit': 'T'}.get(g['outcome'], '?')
        if mark == 'W' and g['win_type'] == 'quota':
            mark += g['win_type'][0]
        grid[(mod, house, int(mission))] += mark
    missions = sorted({m for _, _, m in grid})
    if missions:
        print('| Mod | House | ' + ' | '.join(f'm{m}' for m in missions) + ' |')
        print('|---|---|' + '---|' * len(missions))
        for mod in ('vanilla', 'dunecity'):
            for house in ('atreides', 'harkonnen', 'ordos'):
                if any((mod, house, m) in grid for m in missions):
                    print(f'| {mod} | {house} | ' + ' | '.join(grid.get((mod, house, m), '') for m in missions) + ' |')
        print()


def shard(text):
    k, n = (int(x) for x in text.split('/'))
    if not 1 <= k <= n:
        raise argparse.ArgumentTypeError('--shard K/N with 1 <= K <= N')
    return k, n


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--tier', choices=('smoke', 'full'), default='full')
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--results', help='Directory for games.csv and houses.csv (games already there are skipped)')
parser.add_argument('--scratch', help='Directory for the probe binary and the (deleted) run directories '
                                      '(default: leased agent scratch, removed after a successful batch)')
parser.add_argument('--categories', nargs='+', choices=CATEGORIES, help='Only these categories')
parser.add_argument('--seeds-per-cell', type=int, help='Full tier: at most this many seeds per cell')
parser.add_argument('--cells', help='Only cells whose category/name/mod matches this regular expression')
parser.add_argument('--ladder-seeds', type=int, default=LADDER_SEEDS,
                    help='Full tier: seeds per ladder cell (4 maps, 2 sides alternating; 18 = 72 games per pair)')
parser.add_argument('--shard', type=shard, help='K/N: play every N-th scheduled game starting with the K-th')
parser.add_argument('--jobs', type=int, default=3, help='Games played in parallel')
parser.add_argument('--wall-timeout', type=int, default=1800, help='Wall seconds per game')
parser.add_argument('--fresh', action='store_true', help='Delete --results first (ctest)')
parser.add_argument('--list', action='store_true', help='Print the scheduled games and exit')
parser.add_argument('--report', dest='report_dirs', nargs='+', metavar='RESULTS',
                    help='Print the summary table of these results directories')
parser.add_argument('--draw-score', type=match_score.check_draw_score, default=match_score.DRAW_SCORE)
args = parser.parse_args()
if args.report_dirs:
    report(args)
elif args.list:
    plan = schedule(args)
    for g in plan:
        print(g['id'], g['map'], ' '.join(f'{h}:{t}:{c}' for h, t, c in g['sides']))
    counts = defaultdict(int)
    for g in plan:
        counts[g['category']] += 1
    print(f'{len(plan)} games: ' + ', '.join(f'{c} {n}' for c, n in counts.items()))
elif not args.results:
    parser.error('--results is required to play games')
else:
    run(args)
