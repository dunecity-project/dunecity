#!/usr/bin/env python3
"""Vanilla matches from a Construction Yard start (docs/formations.md, "Scenario tests").

The game Stefan plays: the vanilla mod, a bundled multiplayer map file started as the custom-game
lobby starts it (the map's own MCV or Construction Yard, starting units and credits, its tech
level, the lobby's default game options), every house on its own team. Each house is a QuantBot
of --difficulty with a duel label's test-only force flags (none = the rules before protocol 78,
none+BWFCQ = the 1.0.844 default; formation-duel.py lists the flags) or, with the label "ai", the
classic AI Player (Hard). The engine assigns the start positions from the match seed, so a 1v1
plays every seed twice with the two labels swapped between the same two houses: house, start
and map differences cancel within the pair.

Modes:
  --pairs x:y        1v1 on every --maps entry and seed (house pair cycled by seed, or
                     --all-houses), both label assignments.
  --ffa x,y          free-for-all: half the houses per label, every balanced assignment per seed.
  --ffa a,b,c,d      one label per house: rotations so each label plays every house.
  --ffa x            one label for every house: one self-play game per map and seed.
  --houses H [...]   with --ffa: these playing houses, in slot order (default Harkonnen, Atreides,
                     Ordos, Sardaukar). All six houses give genuine six-player self-play.
  --tech N [N ...]   1v1 at each of these tech levels: a copy of every map with [BASIC] TechLevel=N
                     (the lobby starts a map at its own tech level; games.csv names it <map>@t<N>).

Maps: a bundled multiplayer map (MAPS or a file stem), or rs<size>-<mod> (e.g. rs160-dunecity,
rs160-vanilla): a generated multiplayer Random Skirmish map with one start per house, each a
Fortress with a 20,000-credit army and in DuneCity a 40,000 city (Rock High, Spice Medium) -
Stefan's 4P DuneCity free-for-all - from the seed (map seed and match seed), played in that mod
with --game-options (default his options, tests/performance/data/stefan-game-options.ini).

Labels: as formation-duel.py, plus "default" (the configured rules), a test-only play style
suffix @style (tests/ai/play-styles/<style>.ini, docs/ai-play-styles.md), e.g. default@rush, and a
strategy suffix #random or #<Mix>-<Tempo>[-fixed], e.g. default#QuadMass-Swarm (houses.csv records
the strategy each house played).

Each game appends a row to games.csv, its houses to houses.csv (with the starting mix each QuantBot
played, start_mix, from its QUANTBOT_STRATEGY line), a per-minute sample of every house (credits,
army, harvesters, structures, factories, kill and loss value) to samples.csv, where each house's
army stands each minute (FFA_ARMY_SAMPLE: inside or outside its base, per QuantBot role, at the
bundle or perimeter, the outnumbered verdict) to army.csv, its waves to waves.csv and the
composition per unit-mix slot every 10 game minutes and at the end (value built, lost and
standing, combat reward; docs/ai-play-styles.md, section 8) to mix.csv; the run directory is
deleted straight away and games already in games.csv are skipped. --report prints the tables
used in docs/formations.md, scored by tests/ai/match_score.py (placement score, time-outs
negative, verdict on the paired difference; --scoring legacy for the old rule).
"""
import argparse
import csv
import itertools
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
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402
import match_score  # noqa: E402

MAP_DIR = root / 'data/maps/multiplayer'
# Short names of the bundled maps used by the scenario tests (any other file stem in MAP_DIR
# also works: the part after the last ' - ', lower case, spaces as hyphens, e.g. face-off).
MAPS = {
    'habbanya-penny': '2P - 128x128 - 1v1 - Habbanya-Penny.ini',
    'ergun-sahij': '2P - 128x64 - 1v1 - Ergun-Sahij.ini',
    'tuono-soolin': '2P - 51x128 - 1v1 - Tuono-Soolin.ini',
    'sihaya-pip': '2P - 128x31 - 1v1 - Sihaya-Pip.ini',
    'x-factor': '2P - 32x32 - X-Factor.ini',
}
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
HOUSE_PAIRS = (('atreides', 'harkonnen'), ('ordos', 'atreides'), ('harkonnen', 'ordos'))
FFA_HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar')
KEY = ('tag', 'difficulty', 'map', 'seed', 'sides')
GAME_FIELDS = KEY + ('players', 'winner', 'winner_label', 'end_s', 'waves', 'wall_s', 'error')
DUEL_HOUSE_FIELDS = ('alive', 'kill_value', 'loss_value', 'structures_destroyed', 'units_destroyed', 'structures',
                     'army_value', 'army_units', 'home_kill', 'home_loss', 'focus', 'converges', 'bundle_orders',
                     'gate_holds', 'gate_timeouts', 'raid_orders', 'turret_targets', 'press_on_orders',
                     'finish_waves', 'fortified_holds', 'sonic_retargets', 'surplus_peak', 'siege_orders',
                     'siege_holds', 'assault_holds', 'assault_waves', 'strategy')
HOUSE_ROW_FIELDS = KEY + ('slot', 'house', 'label', 'eliminated_s', 'place') + DUEL_HOUSE_FIELDS + ('start_mix',)
SAMPLE_FIELDS = ('minute', 'credits', 'army_value', 'army_units', 'harvesters', 'structures', 'factories',
                 'kill_value', 'loss_value', 'alive')
MIX_FIELDS = ('minute', 'final', 'built', 'lost', 'army', 'reward')
MIX_ROW_FIELDS = KEY + ('house', 'label') + MIX_FIELDS
SAMPLE_ROW_FIELDS = KEY + ('house', 'label') + SAMPLE_FIELDS
WAVE_FIELDS = ('house', 'launched', 'members', 'engaged', 'arrived', 'lost', 'contact_s', 'shots30', 'loss60',
               'kill60', 'survivors', 'lost_before_contact', 'together_at_contact', 'engaged10')
WAVE_ROW_FIELDS = KEY + ('label',) + WAVE_FIELDS
ARMY_FIELDS = ('minute', 'army_value', 'inside_value', 'outside_value', 'on_road_value', 'wave_value', 'defence_value',
               'free_value', 'free_inside_value', 'at_bundle_value', 'at_perimeter_value', 'stuck_value', 'outnumbered',
               'garrisoned', 'own_estimate', 'enemy_sum', 'enemy_strongest', 'bundle_base_tiles', 'perimeter_base_tiles')
ARMY_ROW_FIELDS = KEY + ('house', 'label') + ARMY_FIELDS
SKIRMISH_OPTIONS = root / 'tests/performance/data/stefan-game-options.ini'
lock = threading.Lock()


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
            with path.open(newline='') as handle:
                header = next(csv.reader(handle), [])
            if list(header) != list(fields):
                # Written by an older harness: rewrite it with the current columns (missing ones empty).
                old = read(path)
                with path.open('w', newline='') as handle:
                    writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
                    writer.writeheader()
                    writer.writerows(old)
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def skirmish(name):
    """rs<size>-<mod> -> (size, mod), or None for a bundled map."""
    match = re.fullmatch(r'rs(\d+)-(vanilla|dunecity)', name)
    return (int(match.group(1)), match.group(2)) if match else None


def map_path(name):
    """The map file of a --maps entry; an '@t<N>' suffix (--tech) names the same map."""
    name = name.split('@t', 1)[0]
    if name in MAPS:
        return MAP_DIR / MAPS[name]
    for path in MAP_DIR.glob('*.ini'):
        if path.stem.rsplit(' - ', 1)[-1].lower().replace(' ', '-') == name.lower():
            return path
    raise SystemExit(f'Unknown map {name}')


def map_tech(name):
    """The tech level of a scheduled map entry ('<map>@t<N>', --tech), or None for the map's own."""
    return int(name.split('@t', 1)[1]) if '@t' in name else None


def game_map(name, scratch):
    """The map file a game plays: the bundled file, or (--tech) a copy of it with [BASIC] TechLevel=N
    in the scratch directory (the lobby starts the map at its own tech level)."""
    path, tech = map_path(name), map_tech(name)
    if tech is None:
        return path
    copy = Path(scratch) / 'maps' / f't{tech}' / path.name
    with lock:
        if not copy.exists():
            lines = path.read_text(errors='replace').splitlines()
            section, out, done = '', [], False
            for line in lines:
                stripped = line.strip()
                if stripped.startswith('['):
                    if section == 'basic' and not done:
                        out.append(f'TechLevel={tech}')
                        done = True
                    section = stripped.strip('[]').strip().lower()
                elif section == 'basic' and stripped.split('=', 1)[0].strip().lower() == 'techlevel':
                    line, done = f'TechLevel={tech}', True
                out.append(line)
            if not done:
                out.append(f'TechLevel={tech}')
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_text('\n'.join(out) + '\n')
    return copy


def side_list(sides):
    """'harkonnen:none+BWFCQ|atreides:none' -> [(house, label), ...] in slot order."""
    return [tuple(side.split(':', 1)) for side in sides.split('|')]


def schedule(args):
    games = []
    if args.ffa:
        labels = args.ffa.split(',')
        houses = tuple(args.houses) if args.houses else FFA_HOUSES
        if len(labels) == 1:
            # One label for every house: one game per map and seed.
            for seed in args.seeds:
                for name in args.maps:
                    games.append((args.tag, args.difficulty, name, str(seed),
                                  '|'.join(f'{h}:{labels[0]}' for h in houses)))
            return games
        if len(labels) == len(houses):
            # One label per house: rotate all assignments, keeping every seed/start paired.
            for seed in args.seeds:
                for name in args.maps:
                    for shift in range(len(houses)):
                        sides = '|'.join(f'{h}:{labels[(i + shift) % len(houses)]}' for i, h in enumerate(houses))
                        games.append((args.tag, args.difficulty, name, str(seed), sides))
            return games
        first, second = labels
        partitions = list(itertools.combinations(houses, len(houses) // 2))
        for seed in args.seeds:
            for name in args.maps:
                for group in partitions:
                    sides = '|'.join(f'{h}:{first if h in group else second}' for h in houses)
                    games.append((args.tag, args.difficulty, name, str(seed), sides))
        return games
    pairs = list(itertools.combinations(args.houses, 2)) if args.houses else HOUSE_PAIRS
    for index, seed in enumerate(args.seeds, args.house_offset):
        houses = pairs if args.all_houses else (pairs[index % len(pairs)],)
        for house_a, house_b in houses:
            for map_index, base in enumerate(args.maps):
                # --seed-per-map: every map its own seed, so a seeded per-house draw differs by map.
                game_seed = seed + map_index * args.seed_per_map
                # --tech: the same map and seed at each tech level ('<map>@t<N>').
                for name in ([f'{base}@t{tech}' for tech in args.tech] if args.tech else [base]):
                    for pair in args.pairs.split(','):
                        x, y = pair.split(':')
                        for la, lb in ((x, y), (y, x)):
                            games.append((args.tag, args.difficulty, name, str(game_seed), f'{house_a}:{la}|{house_b}:{lb}'))
    return games


def build_probe(args, scratch):
    probe = scratch / 'probe'
    if not args.rebuild:
        try:
            binary = probe_binary(probe)
            if binary.exists():
                return binary
        except OSError:
            pass
    shutil.rmtree(probe, ignore_errors=True)
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
                    '--output-dir', str(probe), '--custom-map', str(map_path('x-factor')),
                    '--match-sides', 'harkonnen:none,atreides:none', '--local-spectator', '--enemy-difficulty', 'hard',
                    '--partner-difficulty', 'hard', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(probe / 'profile', ignore_errors=True)
    return probe_binary(probe)



def play(args, scratch, binary, key):
    tag, difficulty, name, seed, sides_text = key
    sides = side_list(sides_text)
    out = scratch / f'{name}-{seed}-{abs(hash(sides_text)) % 10**8}-{tag}'
    shutil.rmtree(out, ignore_errors=True)
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(out),
               '--house', sides[0][0], '--match-sides', ','.join(f'{h}:{l}' for h, l in sides),
               '--local-spectator', '--enemy-difficulty', difficulty, '--partner-difficulty', difficulty,
               '--minutes', str(args.minutes), '--seed', seed, '--capture-mib', '256',
               '--wall-timeout', str(args.wall_timeout), '--path-workers', '1']
    if generated := skirmish(name):
        size, mod = generated
        command += ['--skirmish-ffa-size', str(size), '--skirmish-map-seed', seed, '--mod', mod,
                    '--game-options', str(args.game_options or SKIRMISH_OPTIONS)]
    else:
        command += ['--custom-map', str(game_map(name, scratch))]
        if args.game_options:
            command += ['--game-options', str(args.game_options)]
    if not args.ai_telemetry:
        command.append('--no-ai-telemetry')
    started = time.time()
    outcome = subprocess.run(command, cwd=root, capture_output=True, text=True)
    wall = round(time.time() - started)
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    label_of = {HOUSE_NUMBERS[h]: l for h, l in sides}
    houses = {int(parse(l, 'FORMATION_DUEL_HOUSE:')['house']): parse(l, 'FORMATION_DUEL_HOUSE:')
              for l in lines if 'FORMATION_DUEL_HOUSE:' in l}
    result = next((parse(l, 'FORMATION_DUEL_RESULT:') for l in lines if 'FORMATION_DUEL_RESULT:' in l), {})
    eliminated = {int(e['house']): float(e['end_s']) for e in
                  (parse(l, 'VANILLA_MATCH_ELIMINATED:') for l in lines if 'VANILLA_MATCH_ELIMINATED:' in l)}
    row = dict(zip(KEY, key))
    winner = result.get('winner', '')
    row.update(players=len(sides), winner=winner, winner_label=label_of.get(int(winner), '') if winner not in ('', '-1') else '',
               end_s=result.get('end_s', ''), wall_s=wall, waves=sum('FORMATION_WAVE:' in l for l in lines),
               error='' if outcome.returncode == 0 and result else f'rc={outcome.returncode}')
    results = Path(args.results)
    if row['error']:
        (results / f'failed-{out.name}.txt').write_text(
            outcome.stdout[-4000:] + '\n' + outcome.stderr[-4000:] + '\n' + '\n'.join(lines[-60:]))
        finish_game(scratch, out, True)
        return row
    numbers = {HOUSE_NUMBERS[h]: h for h, _ in sides}
    place = {h: p.place for h, p in match_score.score_game([HOUSE_NUMBERS[h] for h, _ in sides], eliminated).items()}
    starts = {int(s['house']): s.get('start_mix', '') for s in
              (parse(l, 'QUANTBOT_STRATEGY:') for l in lines if 'QUANTBOT_STRATEGY:' in l)}
    house_rows = []
    for slot, (house, label) in enumerate(sides, 1):
        number = HOUSE_NUMBERS[house]
        data = houses.get(number, {})
        house_rows.append({**dict(zip(KEY, key)), 'slot': slot, 'house': house, 'label': label,
                           'eliminated_s': eliminated.get(number, ''), 'place': place[number],
                           **{f: data.get(f, '') for f in DUEL_HOUSE_FIELDS}, 'start_mix': starts.get(number, '')})
    samples = []
    for l in lines:
        if 'ELIMINATION_SAMPLE:' not in l:
            continue
        sample = parse(l, 'ELIMINATION_SAMPLE:')
        number = int(sample['house'])
        if number not in numbers:
            continue
        samples.append({**dict(zip(KEY, key)), 'house': numbers[number], 'label': label_of[number],
                        **{f: sample.get(f, '') for f in SAMPLE_FIELDS}})
    waves = []
    for l in lines:
        if 'FORMATION_WAVE:' not in l:
            continue
        wave = parse(l, 'FORMATION_WAVE:')
        waves.append({**dict(zip(KEY, key)), 'label': label_of.get(int(wave['house']), ''),
                      **{k: wave.get(k, '') for k in WAVE_FIELDS}})
    army = []
    for l in lines:
        if 'FFA_ARMY_SAMPLE:' not in l:
            continue
        sample = parse(l, 'FFA_ARMY_SAMPLE:')
        number = int(sample['house'])
        if number in numbers:
            army.append({**dict(zip(KEY, key)), 'house': numbers[number], 'label': label_of[number],
                         **{f: sample.get(f, '') for f in ARMY_FIELDS}})
    mixes = []
    for l in lines:
        if 'UNIT_MIX_SAMPLE:' not in l:
            continue
        sample = parse(l, 'UNIT_MIX_SAMPLE:')
        number = int(sample['house'])
        if number in numbers:
            mixes.append({**dict(zip(KEY, key)), 'house': numbers[number], 'label': label_of[number],
                          **{f: sample.get(f, '') for f in MIX_FIELDS}})
    append(results / 'houses.csv', HOUSE_ROW_FIELDS, house_rows)
    append(results / 'samples.csv', SAMPLE_ROW_FIELDS, samples)
    append(results / 'waves.csv', WAVE_ROW_FIELDS, waves)
    append(results / 'army.csv', ARMY_ROW_FIELDS, army)
    append(results / 'mix.csv', MIX_ROW_FIELDS, mixes)
    append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, False)
    return row


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {tuple(r[k] for k in KEY) for r in read(results / 'games.csv')}
    todo = [key for key in schedule(args) if key not in done]
    print(f'{len(todo)} games to play ({len(done)} already in games.csv)', flush=True)
    if not todo:
        return
    require_free_space()
    with scratch_dir('vanilla-match', args.scratch) as scratch:
        binary = build_probe(args, scratch)
        started = time.time()
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(play, args, scratch, binary, key) for key in todo]
            for count, future in enumerate(as_completed(futures), 1):
                row = future.result()
                print(f'[{count}/{len(todo)} {round(time.time() - started)} s] {row["map"]}/{row["seed"]} {row["sides"]} -> '
                      f'{row["winner_label"] or "time-out"} ({row["end_s"]} s game, {row["wall_s"]} s wall)'
                      f'{" " + row["error"] if row["error"] else ""}', flush=True)


# ---- Report ---------------------------------------------------------------------------

def number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def by_game(rows):
    grouped = defaultdict(list)
    for row in rows:
        grouped[tuple(row[k] for k in KEY)].append(row)
    return grouped


def placings(game, rows, draw_score):
    """match_score Placing per house row of a game (every house its own team). A decided game's
    houses other than the winner count as eliminated at the end if their elimination line is missing."""
    eliminated = {r['house']: number(r['eliminated_s']) for r in rows if r['eliminated_s'] != ''}
    if game['winner'] not in ('', '-1'):
        for r in rows:
            if HOUSE_NUMBERS[r['house']] != int(game['winner']):
                eliminated.setdefault(r['house'], number(game['end_s']))
    return match_score.score_game([r['house'] for r in rows], eliminated, draw_score)


def pair_scores(games, houses, x, y, args):
    """Per 1v1 game with labels x and y: (game, x's score, strict score, x's house row, y's row,
    y's score, drawn). The score is the placement score, or the old one with --scoring legacy."""
    per_game = by_game(houses)
    out = []
    for game in games:
        if game['error'] or game['players'] != '2':
            continue
        labels = [l for _, l in side_list(game['sides'])]
        if sorted(labels) != sorted([x, y]):
            continue
        house_rows = per_game[tuple(game[k] for k in KEY)]
        rows = {r['label']: r for r in house_rows}
        mine, theirs = rows[x], rows[y]
        score, strict = match_score.legacy_score(game['winner_label'] == x, game['winner_label'] == y,
                                                 number(mine['kill_value']), number(theirs['kill_value']), args.adjudicate)
        other = 1.0 - score
        drawn = not game['winner_label']
        if args.scoring == 'placement':
            placing = placings(game, house_rows, args.draw_score)
            score, other, drawn = placing[mine['house']].score, placing[theirs['house']].score, placing[mine['house']].drawn
        out.append((game, score, strict, mine, theirs, other, drawn))
    return out


def compare(rows, args):
    return match_score.compare([(r[1], r[5], r[6], r[6]) for r in rows], 20261017, args.scoring)


def fmt_ci(rows, args):
    """'score | interval' (legacy) or 'score | opponent's score | difference (interval)' (placement)."""
    c = compare(rows, args)
    if args.scoring == 'legacy':
        return f'{c.candidate:.3f} | {c.low:.2f}-{c.high:.2f}'
    return f'{c.candidate:.3f} | {c.control:.3f} | {c.difference:+.3f} ({c.low:+.2f}..{c.high:+.2f})'


def score_columns(args):
    return 'Score | 95% CI' if args.scoring == 'legacy' else 'Score | Opponent score | Difference (95% CI)'


def write_scores(games, houses, args):
    per_game = by_game(houses)
    rows = []
    for game in games:
        if game['error']:
            continue
        house_rows = per_game[tuple(game[k] for k in KEY)]
        placing = placings(game, house_rows, args.draw_score)
        for r in house_rows:
            p = placing[r['house']]
            rows.append({'game': '/'.join(game[k] for k in KEY), 'side': r['slot'], 'house': r['house'], 'label': r['label'],
                         'eliminated': int(p.eliminated), 'eliminated_s': r['eliminated_s'], 'place': p.place,
                         'score': f'{p.score:.4f}', 'drawn': int(p.drawn), 'end_s': game['end_s'],
                         'decisive': int(game['winner'] not in ('', '-1')), 'kill_value': r['kill_value'],
                         'loss_value': r['loss_value']})
    match_score.write_scores(args.scores, rows)


def baseline_table(args, games, houses):
    """Each pairing x:y against --baseline B: x's side paired with B's side in B's games against the
    same y with the same difficulty, map, seed and houses (e.g. a candidate and the old default, each
    against the old rules)."""
    setup = lambda r, t: (r['difficulty'], r['map'], r['seed'], r['house'], t['house'])
    print(f'\nPaired against {args.baseline} (each side against the same opponent, same difficulty, map, seed and '
          f'houses; difference = own score - {args.baseline}\'s, 95% bootstrap; time-outs own / baseline):\n')
    print(f'| Pairing | Paired games | Score | {args.baseline} score | Difference (95% CI) | Time-outs | Verdict |')
    print('|---|---|---|---|---|---|---|')
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        if x == args.baseline:
            continue
        base = {}
        for r in pair_scores(games, houses, args.baseline, y, args):
            base.setdefault(setup(r[3], r[4]), r)
        paired = [(r[1], base[setup(r[3], r[4])][1], r[6], base[setup(r[3], r[4])][6])
                  for r in pair_scores(games, houses, x, y, args) if setup(r[3], r[4]) in base]
        c = match_score.compare(paired, 20261017, 'placement')
        if c:
            print(f'| {x} v {y} | {c.n} | {c.candidate:.3f} | {c.control:.3f} | {c.difference:+.3f} '
                  f'({c.low:+.2f}..{c.high:+.2f}) | {100 * c.candidate_draws:.0f}% / {100 * c.control_draws:.0f}% | '
                  f'{c.verdict} {args.baseline} |')


def baseline_tag_table(args, games, houses, base_games, base_houses):
    """Each pairing x:y against --baseline-tag T: x's side paired with x's side in T's game of the same
    pairing with the same difficulty, map, seed and houses (the same setup played by another code version)."""
    setup = lambda r, t: (r['difficulty'], r['map'], r['seed'], r['house'], t['house'])
    print(f'\nPaired against tag {args.baseline_tag} (the same pairing and side in the same setup - difficulty, map, '
          f'seed and houses - played by the baseline tag\'s code; difference = own score - baseline\'s, 95% bootstrap; '
          f'time-outs own / baseline):\n')
    print(f'| Pairing | Paired games | Score | Tag {args.baseline_tag} score | Difference (95% CI) | Time-outs | Verdict |')
    print('|---|---|---|---|---|---|---|')
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        base = {}
        for r in pair_scores(base_games, base_houses, x, y, args):
            base.setdefault(setup(r[3], r[4]), r)
        paired = [(r[1], base[setup(r[3], r[4])][1], r[6], base[setup(r[3], r[4])][6])
                  for r in pair_scores(games, houses, x, y, args) if setup(r[3], r[4]) in base]
        c = match_score.compare(paired, 20261017, 'placement')
        if c:
            print(f'| {x} v {y} | {c.n} | {c.candidate:.3f} | {c.control:.3f} | {c.difference:+.3f} '
                  f'({c.low:+.2f}..{c.high:+.2f}) | {100 * c.candidate_draws:.0f}% / {100 * c.control_draws:.0f}% | '
                  f'{c.verdict} tag {args.baseline_tag} |')


def report_pairs(args, games, houses, samples, waves):
    print(f'{match_score.describe(args.scoring, args.draw_score, args.adjudicate)}.\n')
    base_games = [g for g in games if g['tag'] == args.baseline_tag]
    base_houses = [h for h in houses if h['tag'] == args.baseline_tag]
    games = [g for g in games if g['tag'] != args.baseline_tag]
    houses = [h for h in houses if h['tag'] != args.baseline_tag]
    if args.scores:
        write_scores(games, houses, args)
    print(f'| Pairing (first label\'s side) | Games | {score_columns(args)} | Strict | Elim. W-L / time-outs | '
          'Win min (median) | Loss min (median) | Structures lost / destroyed per game | Verdict |')
    print('|---|---|---|---|---|---|---|---|---|---|' + ('' if args.scoring == 'legacy' else '---|'))
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        rows = pair_scores(games, houses, x, y, args)
        if not rows:
            continue
        strict = [r[2] for r in rows]
        wins = [number(r[0]['end_s']) / 60 for r in rows if r[2] == 1.0]
        losses = [number(r[0]['end_s']) / 60 for r in rows if r[2] == 0.0]
        lost = statistics.mean(number(r[4]['structures_destroyed']) for r in rows)
        destroyed = statistics.mean(number(r[3]['structures_destroyed']) for r in rows)
        print(f'| {x} v {y} | {len(rows)} | {fmt_ci(rows, args)} | {statistics.mean(strict):.3f} | '
              f'{len(wins)}-{len(losses)} / {len(rows) - len(wins) - len(losses)} | '
              f'{statistics.median(wins) if wins else 0:.1f} | {statistics.median(losses) if losses else 0:.1f} | '
              f'{lost:.1f} / {destroyed:.1f} | {compare(rows, args).verdict} {y} |')
    if args.baseline:
        baseline_table(args, games, houses)
    if args.baseline_tag:
        baseline_tag_table(args, games, houses, base_games, base_houses)
    # Per map (and difficulty) breakdown.
    print(f'\n| Pairing | Map | Difficulty | Games | {score_columns(args)} | Elim. W-L / time-outs | Median end min |')
    print('|---|---|---|---|---|---|---|---|' + ('' if args.scoring == 'legacy' else '---|'))
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        rows = pair_scores(games, houses, x, y, args)
        groups = defaultdict(list)
        for row in rows:
            groups[(row[0]['map'], row[0]['difficulty'])].append(row)
        for (name, difficulty), group in sorted(groups.items()):
            w = sum(r[2] == 1.0 for r in group)
            l = sum(r[2] == 0.0 for r in group)
            print(f'| {x} v {y} | {name} | {difficulty} | {len(group)} | {fmt_ci(group, args)} | {w}-{l} / {len(group) - w - l} | '
                  f'{statistics.median(number(g["end_s"]) / 60 for g, *_ in group):.1f} |')
    economy(args, samples)
    wave_table(args, waves, games)


def economy(args, samples):
    """Mean economy per label at fixed minutes, over the games still running at that minute."""
    minutes = [int(m) for m in args.minutes_shown.split(',')]
    # The end-of-match sample repeats a minute's sample when a match ends on a minute boundary.
    unique = {tuple(s[k] for k in KEY) + (s['house'], s['minute']): s for s in samples}
    by = defaultdict(list)
    for s in unique.values():
        if s['alive'] == '1' or int(s['minute']) == 0:
            by[(s['label'], int(s['minute']))].append(s)
    labels = sorted({s['label'] for s in samples})
    print('\nEconomy (mean per house alive at that minute: credits / army value (units) / harvesters / '
          'structures / factories; n = houses):')
    print('| Label | ' + ' | '.join(f'min {m}' for m in minutes) + ' |')
    print('|---|' + '---|' * len(minutes))
    for label in labels:
        cells = []
        for m in minutes:
            rows = by.get((label, m), [])
            if not rows:
                cells.append('-')
                continue
            mean = lambda f: statistics.mean(number(r[f]) for r in rows)
            cells.append(f'{mean("credits"):.0f} / {mean("army_value"):.0f} ({mean("army_units"):.0f}) / '
                         f'{mean("harvesters"):.1f} / {mean("structures"):.0f} / {mean("factories"):.1f} n={len(rows)}')
        print(f'| {label} | ' + ' | '.join(cells) + ' |')


def wave_table(args, waves, games):
    count = defaultdict(int)
    for g in games:
        if not g['error']:
            for _, label in side_list(g['sides']):
                count[label] += 1
    by = defaultdict(list)
    for w in waves:
        by[w['label']].append(w)
    print('\n| Label | Houses | Waves / house | First wave min (median) | Members | Engaged ≤10 s | Trade 60 s | Contact s |')
    print('|---|---|---|---|---|---|---|---|')
    for label, rows in sorted(by.items()):
        firsts = defaultdict(lambda: 1e9)
        for w in rows:
            game = tuple(w[k] for k in KEY) + (w['house'],)
            firsts[game] = min(firsts[game], number(w['launched']) / 3750)
        members = sum(number(w['members']) for w in rows)
        engaged = sum(max(0.0, number(w['engaged10'])) for w in rows)
        killed = sum(number(w['kill60']) for w in rows)
        lost = sum(number(w['loss60']) for w in rows)
        contact = [number(w['contact_s']) for w in rows if number(w['contact_s'], -1) >= 0]
        print(f'| {label} | {count[label]} | {len(rows) / max(1, count[label]):.1f} | '
              f'{statistics.median(firsts.values()):.1f} | {members / len(rows):.1f} | '
              f'{100 * engaged / max(1, members):.0f}% | {killed / max(1, lost):.2f} | '
              f'{statistics.median(contact) if contact else 0:.0f} |')


def report_ffa(args, games, houses):
    labels = args.ffa.split(',')
    control = args.control or (labels[1] if len(labels) > 1 else labels[0])
    legacy = args.scoring == 'legacy'
    rows = [h for h in houses if h['tag'] in args.tags and h['difficulty'] == args.difficulty]
    valid = {tuple(g[k] for k in KEY): g for g in games if not g['error'] and g['players'] != '2'}
    rows = [h for h in rows if tuple(h[k] for k in KEY) in valid]
    if args.scores:
        write_scores(list(valid.values()), rows, args)
    # Per house: (score, drawn). Legacy: the old place score, 1 first, 0 last; drawn = alive at a time-out.
    scored = {}
    for key, group in by_game(rows).items():
        game = valid[key]
        if legacy:
            for h in group:
                scored[id(h)] = ((len(group) - number(h['place'])) / (len(group) - 1),
                                 not game['winner_label'] and h['eliminated_s'] == '')
        else:
            placing = placings(game, group, args.draw_score)
            for h in group:
                scored[id(h)] = (placing[h['house']].score, placing[h['house']].drawn)
    print(f'{match_score.describe(args.scoring, args.draw_score, args.adjudicate)}.\n')
    print('| Tag | Map | Label | Houses | Mean place | Mean score | Wins (sole survivor) | Eliminated | Alive at a time-out | '
          'Mean elimination min | Kill / loss value per house |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    win_label = {key: g['winner_label'] for key, g in valid.items()}
    for tag in sorted(args.tags):
        for name in sorted({h['map'] for h in rows}) + ['all']:
            for label in labels:
                group = [h for h in rows if h['tag'] == tag and h['label'] == label and (name == 'all' or h['map'] == name)]
                if not group:
                    continue
                wins = sum(1 for h in group if win_label[tuple(h[k] for k in KEY)] == label and h['eliminated_s'] == '')
                elim = [number(h['eliminated_s']) / 60 for h in group if h['eliminated_s'] != '']
                print(f'| {tag} | {name} | {label} | {len(group)} | {statistics.mean(number(h["place"]) for h in group):.2f} | '
                      f'{statistics.mean(scored[id(h)][0] for h in group):+.3f} | '
                      f'{wins} | {len(elim)} | {sum(scored[id(h)][1] for h in group)} | {statistics.mean(elim) if elim else 0:.1f} | '
                      f'{statistics.mean(number(h["kill_value"]) for h in group):.0f} / '
                      f'{statistics.mean(number(h["loss_value"]) for h in group):.0f} |')
    if len(labels) == 1:
        report_ffa_tags(args, valid, rows, scored)
        return
    # Per game, each label's mean over its houses; the candidate against the control in the same games.
    per_game = defaultdict(lambda: defaultdict(list))
    for h in rows:
        per_game[tuple(h[k] for k in KEY)][h['label']].append(scored[id(h)])
    mean = lambda values: (statistics.mean(v[0] for v in values), statistics.mean(1.0 if v[1] else 0.0 for v in values))
    print(f'\nPer game (mean over a label\'s houses), against {control} in the same games '
          f'({"interval of the score against 0.5" if legacy else "difference = score - control score, 95% bootstrap"}):\n')
    print(f'| Label | Games | Score | Control score | {"95% CI" if legacy else "Difference (95% CI)"} | '
          'Alive at a time-out (share of houses) own / control | Verdict |')
    print('|---|---|---|---|---|---|---|')
    for label in labels:
        if label == control:
            continue
        pairs = []
        for by_label in per_game.values():
            if label in by_label and control in by_label:
                (a, da), (b, db) = mean(by_label[label]), mean(by_label[control])
                pairs.append((a, b, da, db))
        c = match_score.compare(pairs, 20261017, args.scoring)
        if not c:
            continue
        interval = f'{c.low:.2f}-{c.high:.2f}' if legacy else f'{c.difference:+.3f} ({c.low:+.2f}..{c.high:+.2f})'
        print(f'| {label} | {c.n} | {c.candidate:+.3f} | {c.control:+.3f} | {interval} | '
              f'{100 * statistics.mean(p[2] for p in pairs):.0f}% / {100 * statistics.mean(p[3] for p in pairs):.0f}% | '
              f'{c.verdict} {control} |')
    timeouts = sum(1 for g in valid.values() if not g['winner_label'])
    print(f'\nTime-outs (two or more houses alive at the limit): {timeouts} of {len(valid)}')


def report_ffa_tags(args, valid, rows, scored):
    """One label for every house: each tag's mean house score per game (in a game of equals this is
    the time-out penalty: -0.25 x survivors / houses, 0 when one house is left) paired with the
    control tag's game on the same map and seed."""
    tags = sorted(args.tags)
    control = args.baseline_tag or tags[0]
    per_game = defaultdict(list)
    for h in rows:
        per_game[tuple(h[k] for k in KEY)].append(scored[id(h)])
    by_setup = defaultdict(dict)
    for key, values in per_game.items():
        game = dict(zip(KEY, key))
        by_setup[(game['difficulty'], game['map'], game['seed'])][game['tag']] = (
            statistics.mean(v[0] for v in values), 0.0 if valid[key]['winner_label'] else 1.0)
    print(f'\nPer game (mean house score), each tag against tag {control} on the same map and seed '
          f'(difference = score - control score, 95% bootstrap; time-out = two or more houses alive at the limit):\n')
    print('| Tag | Games | Score | Control score | Difference (95% CI) | Time-outs own / control | Verdict |')
    print('|---|---|---|---|---|---|---|')
    for tag in tags:
        if tag == control:
            continue
        pairs = [(s[tag][0], s[control][0], s[tag][1] > 0, s[control][1] > 0)
                 for s in by_setup.values() if tag in s and control in s]
        c = match_score.compare(pairs, 20261017, args.scoring)
        if not c:
            continue
        print(f'| {tag} | {c.n} | {c.candidate:+.3f} | {c.control:+.3f} | {c.difference:+.3f} ({c.low:+.2f}..{c.high:+.2f}) | '
              f'{sum(p[2] for p in pairs)} / {sum(p[3] for p in pairs)} | {c.verdict} {control} |')
    for tag in tags:
        group = [g for g in valid.values() if g['tag'] == tag]
        print(f'\nTag {tag} time-outs (two or more houses alive at the limit): '
              f'{sum(1 for g in group if not g["winner_label"])} of {len(group)}')


def longest_inside(rows, share):
    """Longest run of consecutive sampled minutes with at least `share` of the army inside the base."""
    best = run_length = 0
    for r in sorted(rows, key=lambda r: int(r['minute'])):
        army = number(r['army_value'])
        inside = army > 0 and number(r['inside_value']) >= share * army
        run_length = run_length + 1 if inside else 0
        best = max(best, run_length)
    return best


def report_ffa_army(args, games, houses, waves, army):
    """Where the armies stand and how the free-for-all goes, per tag and map (docs/formations.md,
    "Free-for-all: the army deploys")."""
    print('\n| Tag | Map | Games | Ended (one left) | Game min | First elimination min | Eliminated per game | '
          'Waves per house | Wave members (median) | Army outside base | Free reserve inside base | '
          'Garrisoned house-minutes | Longest 80%-inside stretch min (median / max) |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for tag in sorted(args.tags):
        for name in sorted({g['map'] for g in games if g['tag'] == tag}):
            group = [g for g in games if g['tag'] == tag and g['map'] == name and not g['error']]
            if not group:
                continue
            keys = {tuple(g[k] for k in KEY) for g in group}
            hs = [h for h in houses if tuple(h[k] for k in KEY) in keys]
            ws = [w for w in waves if tuple(w[k] for k in KEY) in keys]
            rows = [a for a in army if tuple(a[k] for k in KEY) in keys and int(a['minute']) >= 3
                    and number(a['army_value']) > 0]
            ended = sum(1 for g in group if g['winner'] not in ('', '-1'))
            length = [number(g['end_s']) / 60 for g in group]
            firsts = []
            for key in keys:
                elim = [number(h['eliminated_s']) / 60 for h in hs if tuple(h[k] for k in KEY) == key and h['eliminated_s']]
                firsts.append(min(elim) if elim else float('inf'))
            finite = [f for f in firsts if f != float('inf')]
            first_text = (f'{statistics.median(firsts):.1f}' if len(finite) * 2 > len(firsts) else f'>{args.minutes}') + \
                f' ({len(finite)}/{len(firsts)} games)'
            outside = sum(number(a['outside_value']) for a in rows) / max(1, sum(number(a['army_value']) for a in rows))
            free = sum(number(a['free_value']) for a in rows)
            free_inside = sum(number(a['free_inside_value']) for a in rows) / max(1, free)
            garrisoned = sum(1 for a in rows if a['garrisoned'] == '1') / max(1, len(rows))
            stretches = []
            for key in keys:
                for house in {a['house'] for a in rows if tuple(a[k] for k in KEY) == key}:
                    stretches.append(longest_inside([a for a in rows if tuple(a[k] for k in KEY) == key
                                                     and a['house'] == house], 0.8))
            members = [number(w['members']) for w in ws]
            print(f'| {tag} | {name} | {len(group)} | {ended} | {statistics.mean(length):.1f} | {first_text} | '
                  f'{sum(1 for h in hs if h["eliminated_s"]) / len(group):.2f} | {len(ws) / max(1, len(hs)):.1f} | '
                  f'{statistics.median(members) if members else 0:.0f} | {outside:.0%} | {free_inside:.0%} | '
                  f'{garrisoned:.0%} | {statistics.median(stretches) if stretches else 0:.0f} / '
                  f'{max(stretches) if stretches else 0} |')


def report(args):
    results = Path(args.results)
    tags = set(args.tags or [args.tag]) | ({args.baseline_tag} if args.baseline_tag else set())
    args.tags = tags
    keep = lambda r: r['tag'] in tags and (args.difficulty_filter is None or r['difficulty'] == args.difficulty_filter)
    extra = Path(args.baseline_results) if args.baseline_results else None
    if extra and extra.resolve() == results.resolve():
        extra = None

    def load(name):
        rows = [r for r in read(results / name) if keep(r)]
        if extra:
            rows += [r for r in read(extra / name) if keep(r) and r['tag'] == args.baseline_tag]
        return rows
    games, houses = load('games.csv'), load('houses.csv')
    if args.ffa:
        report_ffa(args, games, houses)
        report_ffa_army(args, games, houses, load('waves.csv'), load('army.csv'))
        return
    other = lambda rows: [r for r in rows if r['tag'] != args.baseline_tag]
    report_pairs(args, games, houses, other(load('samples.csv')), other(load('waves.csv')))


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--results', required=True,
                    help='Directory for games.csv, houses.csv, samples.csv, army.csv, waves.csv')
parser.add_argument('--scratch', help='Directory for the probe binary and the (deleted) run directories '
                                      '(default: leased agent scratch, removed after a successful batch)')
parser.add_argument('--tag', default='cy1', help='Label of the code variant being measured')
parser.add_argument('--tags', nargs='+', help='Report: the tags to include (default --tag)')
parser.add_argument('--difficulty', choices=('hard', 'brutal'), default='hard')
parser.add_argument('--difficulty-filter', choices=('hard', 'brutal'), help='Report: only this difficulty (default all)')
parser.add_argument('--minutes', type=int, default=80, help='Game minutes before a game is a time-out')
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--maps', nargs='+', default=list(MAPS))
parser.add_argument('--tech', nargs='+', type=int, choices=range(1, 10), metavar='N',
                    help='1v1: play every map at each of these tech levels (a copy of the map with [BASIC] TechLevel=N; '
                         'games.csv names the map <map>@t<N>; tests/ai/tech-bands.py lists where the bands change)')
parser.add_argument('--seeds', nargs='+', type=int, default=[8101, 8202, 8303, 8404, 8505, 8606])
parser.add_argument('--pairs', help='1v1 label pairs x:y,x:y (each played with both assignments)')
parser.add_argument('--ffa', help='Free-for-all: one label for self-play, two labels split evenly, or one per house '
                    '(four houses by default; use --houses for six)')
parser.add_argument('--game-options', type=Path,
                    help='[Game Options] INI for every game (default: the lobby defaults; rs<size>-<mod> maps: '
                         'tests/performance/data/stefan-game-options.ini)')
parser.add_argument('--control', help='FFA report: the label every other label is compared with (default the second --ffa label)')
parser.add_argument('--baseline-tag', help='Report: compare with this tag (another code version, e.g. the last release): '
                                           '1v1 pairings side by side in the same setup; a one-label FFA game by game '
                                           '(default the first tag in sort order)')
parser.add_argument('--baseline-results', help='Directory of the --baseline-tag rows (default --results)')
parser.add_argument('--baseline', help='1v1 report: also pair each first label with this label\'s side against the same '
                                       'opponent in the same setups (e.g. the old default, for a candidate v none)')
parser.add_argument('--all-houses', action='store_true', help='1v1: every seed with all house pairs (see --houses)')
parser.add_argument('--houses', nargs='+', choices=sorted(HOUSE_NUMBERS),
                    help='1v1: every pair of these houses (default Atreides/Harkonnen/Ordos). '
                         'FFA: playing houses in slot order (default Harkonnen/Atreides/Ordos/Sardaukar); '
                         'list all six for six-player self-play')
parser.add_argument('--house-offset', type=int, default=0,
                    help='1v1: the house pair of the first seed (the pair cycles with the seed\'s position plus this), '
                         'so a run split by seed across machines plays the pairs the whole list would')
parser.add_argument('--seed-per-map', type=int, default=0, metavar='STRIDE',
                    help='1v1: map i of --maps plays seed + i * STRIDE (0 = every map the same seed), so a per-game '
                         'draw from seed and house (the Bounded mix) differs by map')
parser.add_argument('--jobs', type=int, default=4, help='Games played in parallel')
parser.add_argument('--ai-telemetry', action='store_true', help='Keep the AI decision log (events.jsonl)')
parser.add_argument('--rebuild', action='store_true', help='Rebuild the probe binary even if one exists')
parser.add_argument('--report', action='store_true')
parser.add_argument('--minutes-shown', default='5,10,15,20,30,40,60', help='Report: economy table minutes')
parser.add_argument('--adjudicate', type=float, default=1000,
                    help='Report, --scoring legacy only: a time-out goes to the side ahead on killed unit value by at '
                         'least this many credits')
match_score.add_arguments(parser)
args = parser.parse_args()
if bool(args.pairs) == bool(args.ffa):
    parser.error('Give exactly one of --pairs and --ffa.')
if args.ffa:
    houses = args.houses or FFA_HOUSES
    labels = args.ffa.split(',')
    if len(houses) < 3 or len(set(houses)) != len(houses):
        parser.error('FFA needs at least three distinct houses.')
    if len(labels) not in (1, 2, len(houses)) or any(not label for label in labels):
        parser.error('FFA needs one label, two balanced labels, or one label per house.')
    if len(labels) == 2 and len(houses) % 2:
        parser.error('Two-label FFA needs an even number of houses.')
if args.report:
    report(args)
else:
    run(args)
