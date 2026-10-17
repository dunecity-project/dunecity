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
  --ffa x,y          four-player free-for-all on the 4P --maps: Harkonnen, Atreides, Ordos and
                     Sardaukar, two houses per label, all six partitions per seed.

Each game appends a row to games.csv, its houses to houses.csv, a per-minute sample of every
house (credits, army, harvesters, structures, factories, kill and loss value) to samples.csv
and its waves to waves.csv; the run directory is deleted straight away and games already in
games.csv are skipped. --report prints the tables used in docs/formations.md.
"""
import argparse
import csv
import itertools
import random
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
from probe_profile import finish_game, require_free_space, scratch_dir  # noqa: E402

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
                     'gate_holds', 'gate_timeouts')
HOUSE_ROW_FIELDS = KEY + ('slot', 'house', 'label', 'eliminated_s', 'place') + DUEL_HOUSE_FIELDS
SAMPLE_FIELDS = ('minute', 'credits', 'army_value', 'army_units', 'harvesters', 'structures', 'factories',
                 'kill_value', 'loss_value', 'alive')
SAMPLE_ROW_FIELDS = KEY + ('house', 'label') + SAMPLE_FIELDS
WAVE_FIELDS = ('house', 'launched', 'members', 'engaged', 'arrived', 'lost', 'contact_s', 'shots30', 'loss60',
               'kill60', 'survivors', 'lost_before_contact', 'together_at_contact', 'engaged10')
WAVE_ROW_FIELDS = KEY + ('label',) + WAVE_FIELDS
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
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def map_path(name):
    if name in MAPS:
        return MAP_DIR / MAPS[name]
    for path in MAP_DIR.glob('*.ini'):
        if path.stem.rsplit(' - ', 1)[-1].lower().replace(' ', '-') == name.lower():
            return path
    raise SystemExit(f'Unknown map {name}')


def side_list(sides):
    """'harkonnen:none+BWFCQ|atreides:none' -> [(house, label), ...] in slot order."""
    return [tuple(side.split(':', 1)) for side in sides.split('|')]


def schedule(args):
    games = []
    if args.ffa:
        first, second = args.ffa.split(',')
        partitions = [group for group in itertools.combinations(FFA_HOUSES, 2)]
        for seed in args.seeds:
            for name in args.maps:
                for group in partitions:
                    sides = '|'.join(f'{h}:{first if h in group else second}' for h in FFA_HOUSES)
                    games.append((args.tag, args.difficulty, name, str(seed), sides))
        return games
    for index, seed in enumerate(args.seeds):
        houses = HOUSE_PAIRS if args.all_houses else (HOUSE_PAIRS[index % len(HOUSE_PAIRS)],)
        for house_a, house_b in houses:
            for name in args.maps:
                for pair in args.pairs.split(','):
                    x, y = pair.split(':')
                    for la, lb in ((x, y), (y, x)):
                        games.append((args.tag, args.difficulty, name, str(seed), f'{house_a}:{la}|{house_b}:{lb}'))
    return games


def build_probe(args, scratch):
    probe = scratch / 'probe'
    binary = probe / 'balance-probe.app/Contents/MacOS/balance-probe'
    if binary.exists() and not args.rebuild:
        return binary
    shutil.rmtree(probe, ignore_errors=True)
    subprocess.run(['python3', str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
                    '--output-dir', str(probe), '--custom-map', str(map_path('x-factor')),
                    '--match-sides', 'harkonnen:none,atreides:none', '--local-spectator', '--enemy-difficulty', 'hard',
                    '--partner-difficulty', 'hard', '--minutes', '1', '--capture-mib', '256', '--no-ai-telemetry'],
                   cwd=root, check=True, capture_output=True, text=True)
    shutil.rmtree(probe / 'profile', ignore_errors=True)
    return binary


def places(sides, eliminated):
    """Place per house: eliminated houses by elimination order (last out = best of them), the
    houses alive at the end share the best places (mean of the places they cover)."""
    alive = [h for h, _ in sides if h not in eliminated]
    out = sorted((h for h, _ in sides if h in eliminated), key=lambda h: -eliminated[h])
    result = {}
    if alive:
        shared = statistics.mean(range(1, len(alive) + 1))
        for house in alive:
            result[house] = shared
    for index, house in enumerate(out, len(alive) + 1):
        result[house] = index
    return result


def play(args, scratch, binary, key):
    tag, difficulty, name, seed, sides_text = key
    sides = side_list(sides_text)
    out = scratch / f'{name}-{seed}-{abs(hash(sides_text)) % 10**8}-{tag}'
    shutil.rmtree(out, ignore_errors=True)
    command = ['python3', str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', args.build_dir,
               '--prebuilt-probe', str(binary), '--output-dir', str(out), '--custom-map', str(map_path(name)),
               '--house', sides[0][0], '--match-sides', ','.join(f'{h}:{l}' for h, l in sides),
               '--local-spectator', '--enemy-difficulty', difficulty, '--partner-difficulty', difficulty,
               '--minutes', str(args.minutes), '--seed', seed, '--capture-mib', '256',
               '--wall-timeout', str(args.wall_timeout), '--path-workers', '1']
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
    place = places([(HOUSE_NUMBERS[h], l) for h, l in sides], eliminated)
    house_rows = []
    for slot, (house, label) in enumerate(sides, 1):
        number = HOUSE_NUMBERS[house]
        data = houses.get(number, {})
        house_rows.append({**dict(zip(KEY, key)), 'slot': slot, 'house': house, 'label': label,
                           'eliminated_s': eliminated.get(number, ''), 'place': place[number],
                           **{f: data.get(f, '') for f in DUEL_HOUSE_FIELDS}})
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
    append(results / 'houses.csv', HOUSE_ROW_FIELDS, house_rows)
    append(results / 'samples.csv', SAMPLE_ROW_FIELDS, samples)
    append(results / 'waves.csv', WAVE_ROW_FIELDS, waves)
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


def bootstrap(scores, rounds=2000):
    """95% interval of the mean score, resampling games (fixed seed: the report is reproducible)."""
    if not scores:
        return (0.0, 0.0)
    rng = random.Random(20261017)
    means = sorted(statistics.mean(rng.choices(scores, k=len(scores))) for _ in range(rounds))
    return means[int(0.025 * rounds)], means[int(0.975 * rounds) - 1]


def by_game(rows):
    grouped = defaultdict(list)
    for row in rows:
        grouped[tuple(row[k] for k in KEY)].append(row)
    return grouped


def pair_scores(games, houses, x, y, margin):
    """Per 1v1 game with labels x and y: (game, x's score, strict score, x's house row, y's row)."""
    per_game = by_game(houses)
    out = []
    for game in games:
        if game['error'] or game['players'] != '2':
            continue
        labels = [l for _, l in side_list(game['sides'])]
        if sorted(labels) != sorted([x, y]):
            continue
        rows = {r['label']: r for r in per_game[tuple(game[k] for k in KEY)]}
        mine, theirs = rows[x], rows[y]
        if game['winner_label']:
            strict = 1.0 if game['winner_label'] == x else 0.0
            score = strict
        else:
            strict = 0.5
            trade = number(mine['kill_value']) - number(theirs['kill_value'])
            score = 1.0 if trade >= margin else (0.0 if trade <= -margin else 0.5)
        out.append((game, score, strict, mine, theirs))
    return out


def fmt_ci(scores):
    low, high = bootstrap(scores)
    return f'{statistics.mean(scores):.3f} | {low:.2f}-{high:.2f}'


def report_pairs(args, games, houses, samples, waves):
    print('| Pairing (first label\'s side) | Games | Score | 95% CI | Strict | Elim. W-L / time-outs | '
          'Win min (median) | Loss min (median) | Structures lost / destroyed per game |')
    print('|---|---|---|---|---|---|---|---|---|')
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        rows = pair_scores(games, houses, x, y, args.adjudicate)
        if not rows:
            continue
        scores = [s for _, s, _, _, _ in rows]
        strict = [s for _, _, s, _, _ in rows]
        wins = [number(g['end_s']) / 60 for g, _, st, _, _ in rows if st == 1.0]
        losses = [number(g['end_s']) / 60 for g, _, st, _, _ in rows if st == 0.0]
        lost = statistics.mean(number(t['structures_destroyed']) for _, _, _, _, t in rows)
        destroyed = statistics.mean(number(m['structures_destroyed']) for _, _, _, m, _ in rows)
        print(f'| {x} v {y} | {len(rows)} | {fmt_ci(scores)} | {statistics.mean(strict):.3f} | '
              f'{len(wins)}-{len(losses)} / {len(rows) - len(wins) - len(losses)} | '
              f'{statistics.median(wins) if wins else 0:.1f} | {statistics.median(losses) if losses else 0:.1f} | '
              f'{lost:.1f} / {destroyed:.1f} |')
    # Per map (and difficulty) breakdown.
    print('\n| Pairing | Map | Difficulty | Games | Score | 95% CI | Elim. W-L / time-outs | Median end min |')
    print('|---|---|---|---|---|---|---|---|')
    for pair in args.pairs.split(','):
        x, y = pair.split(':')
        rows = pair_scores(games, houses, x, y, args.adjudicate)
        groups = defaultdict(list)
        for row in rows:
            groups[(row[0]['map'], row[0]['difficulty'])].append(row)
        for (name, difficulty), group in sorted(groups.items()):
            scores = [s for _, s, _, _, _ in group]
            w = sum(st == 1.0 for _, _, st, _, _ in group)
            l = sum(st == 0.0 for _, _, st, _, _ in group)
            print(f'| {x} v {y} | {name} | {difficulty} | {len(group)} | {fmt_ci(scores)} | {w}-{l} / {len(group) - w - l} | '
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
    first, second = args.ffa.split(',')
    rows = [h for h in houses if h['tag'] in args.tags and h['difficulty'] == args.difficulty]
    valid = {tuple(g[k] for k in KEY) for g in games if not g['error'] and g['players'] != '2'}
    rows = [h for h in rows if tuple(h[k] for k in KEY) in valid]
    print('| Map | Label | Houses | Mean place | Wins (sole survivor) | Eliminated | Mean elimination min | '
          'Kill / loss value per house |')
    print('|---|---|---|---|---|---|---|---|')
    win_label = {tuple(g[k] for k in KEY): g['winner_label'] for g in games}
    for name in sorted({h['map'] for h in rows}) + ['all']:
        for label in (first, second):
            group = [h for h in rows if h['label'] == label and (name == 'all' or h['map'] == name)]
            if not group:
                continue
            wins = sum(1 for h in group if win_label[tuple(h[k] for k in KEY)] == label and h['eliminated_s'] == '')
            elim = [number(h['eliminated_s']) / 60 for h in group if h['eliminated_s'] != '']
            print(f'| {name} | {label} | {len(group)} | {statistics.mean(number(h["place"]) for h in group):.2f} | '
                  f'{wins} | {len(elim)} | {statistics.mean(elim) if elim else 0:.1f} | '
                  f'{statistics.mean(number(h["kill_value"]) for h in group):.0f} / '
                  f'{statistics.mean(number(h["loss_value"]) for h in group):.0f} |')
    # Place as a score: 1 for first, 0 for last, per house; interval over games.
    per_game = defaultdict(list)
    for h in rows:
        if h['label'] == first:
            per_game[tuple(h[k] for k in KEY)].append((4 - number(h['place'])) / 3)
    scores = [statistics.mean(v) for v in per_game.values()]
    if scores:
        print(f'\n{first}: place score (1 = first, 0 = last; mean of its two houses per game) {fmt_ci(scores)} '
              f'over {len(scores)} games')
    timeouts = sum(1 for g in games if tuple(g[k] for k in KEY) in valid and not g['winner_label'])
    print(f'Time-outs (two or more houses alive at the limit): {timeouts} of {len(valid)}')


def report(args):
    results = Path(args.results)
    tags = set(args.tags or [args.tag])
    args.tags = tags
    keep = lambda r: r['tag'] in tags and (args.difficulty_filter is None or r['difficulty'] == args.difficulty_filter)
    games = [g for g in read(results / 'games.csv') if keep(g)]
    houses = [h for h in read(results / 'houses.csv') if keep(h)]
    if args.ffa:
        report_ffa(args, games, houses)
        return
    samples = [s for s in read(results / 'samples.csv') if keep(s)]
    waves = [w for w in read(results / 'waves.csv') if keep(w)]
    report_pairs(args, games, houses, samples, waves)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', default=str(root / 'build'))
parser.add_argument('--results', required=True, help='Directory for games.csv, houses.csv, samples.csv, waves.csv')
parser.add_argument('--scratch', help='Directory for the probe binary and the (deleted) run directories '
                                      '(default: leased agent scratch, removed after a successful batch)')
parser.add_argument('--tag', default='cy1', help='Label of the code variant being measured')
parser.add_argument('--tags', nargs='+', help='Report: the tags to include (default --tag)')
parser.add_argument('--difficulty', choices=('hard', 'brutal'), default='hard')
parser.add_argument('--difficulty-filter', choices=('hard', 'brutal'), help='Report: only this difficulty (default all)')
parser.add_argument('--minutes', type=int, default=80, help='Game minutes before a game is a time-out')
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--maps', nargs='+', default=list(MAPS))
parser.add_argument('--seeds', nargs='+', type=int, default=[8101, 8202, 8303, 8404, 8505, 8606])
parser.add_argument('--pairs', help='1v1 label pairs x:y,x:y (each played with both assignments)')
parser.add_argument('--ffa', help='Four-player free-for-all: two labels x,y, two houses each')
parser.add_argument('--all-houses', action='store_true', help='1v1: every seed with all three house pairs')
parser.add_argument('--jobs', type=int, default=4, help='Games played in parallel')
parser.add_argument('--ai-telemetry', action='store_true', help='Keep the AI decision log (events.jsonl)')
parser.add_argument('--rebuild', action='store_true', help='Rebuild the probe binary even if one exists')
parser.add_argument('--report', action='store_true')
parser.add_argument('--minutes-shown', default='5,10,15,20,30,40,60', help='Report: economy table minutes')
parser.add_argument('--adjudicate', type=float, default=1000,
                    help='Report: a time-out goes to the side ahead on killed unit value by at least this many credits')
args = parser.parse_args()
if bool(args.pairs) == bool(args.ffa):
    parser.error('Give exactly one of --pairs and --ffa.')
if args.report:
    report(args)
else:
    run(args)
