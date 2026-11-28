#!/usr/bin/env python3
"""Mandatory regression match: four QuantBot Brutal houses deploy and fight in a DuneCity free-for-all.

Stefan's 1.0.847 game (4P 160x160 Random 1v3 FFA DuneCity, every house QuantBot Brutal) never
launched a wave: each house judged itself "outnumbered" against the SUM of its three enemies'
remembered armies, so the perimeter hold kept every army packed among its own city blocks for
the whole game (docs/formations.md, "Free-for-all: the army deploys"). This plays generated
4P DuneCity free-for-alls (vanilla-match.py's rs<size>-dunecity: every start a Fortress with a
20,000-credit army and a 40,000 city, his game options), all four houses the configured QuantBot
Brutal, one match per --seeds seed (map and match seed alike, --jobs at a time), and fails when

  * on the first seed (deterministic deploy checks):
    - no ground wave of at least --wave-members units has launched by --wave-minutes;
    - a house keeps its idle reserve (QuantBot free units, at least --reserve-value credits of
      them) at least 80% inside its own base (within 2 tiles of its buildings) for more than
      --inside-minutes consecutive sampled minutes;
  * on no seed has a house been eliminated by --eliminated-minutes.

This self-contained regression explicitly retains its historical, non-pure role
observer. It is never evidence for an A/B or digest comparison; tuning observers
remain pure and do not enable its regression-only flag.

Why the elimination check spans three seeds: whether one particular FFA has eliminated
somebody by minute 30 is chance. Any change that touches the match at all reorders it: about a
third of seeds do (8-9 of 24 in the 1.0.848 sweep). The city-airplane change
(fix/city-airplane, 2026-10-22) moved seed 9101 from 26.9 minutes to no elimination in 45, although
the 12-seed sweep 9101-9112 showed no shift: 6 of 12 seeds eliminated by minute 30 with planes shot
down as before, 5 of 12 with the change (~/Documents/projects/dunecity-results/city-airplane/ffa-sweep).
A single seed only re-pins chance. The 1.0.847 stuck army still fails every seed, because its waves
never leave, so at least one of 9101, 9103 and 9105 has to finish someone; each did in both builds
of that sweep except 9101 with the change.

The deadline is minute 35 since TrooperSupport (2026-11-05, runtime 846c1336): defending houses
now field WOR troopers, so 9103 first eliminated a house at 30.3 and 9105 at 30.6 minutes while
every deploy check passed (19 waves, four by minute 8, largest 70). Controlled OFF/ON on 9105:
Ordos died at 21.2 OFF and survived to 30.6 ON with 14200 army value at minute 20 versus 0
(HANDOVER, docs/quantbot-dunecity-audit.md). The stuck-army failure this guards against never
eliminates anyone within 45 minutes, so minute 35 still catches it.

Observer-only samples can omit role measurements (roles_measured=0 or negative free values).
These fail the reserve measurement prerequisite, not the game-behavior check; historical measured
samples without roles_measured retain their original interpretation.

It is a batch harness (schedule/build_probe/play/report), so the cluster queue can play the seeds
on the Linux workers, one unit per seed, and check the collected results once:

  run-ffa-army-deploys.py --build-dir build --results build/ffa-army-deploys --fresh
  dunecity-queue submit --tag ffa --cwd . --rev SHA --machines aws -- \\
      python3 tests/ai/run-ffa-army-deploys.py --results ~/Documents/projects/dunecity-results/ffa

Each seed's games.csv row says whether the match ran; ffa-<seed>.log keeps the wave, army-sample
and elimination lines the checks read. --output-dir is the old name of --results.
"""
import argparse
import csv
import shutil
import subprocess
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, remove_tree, require_free_space, scratch_dir  # noqa: E402

HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar')
CYCLES_PER_MINUTE = 3750
MARKERS = ('FORMATION_WAVE:', 'FFA_ARMY_SAMPLE:', 'VANILLA_MATCH_ELIMINATED:')
FIELDS = ('id', 'seed', 'wall_s', 'error')


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def read(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def seeds(args):
    return [int(s) for s in args.seeds.split(',') if s.strip()]


def schedule(args):
    return [{'id': f'ffa-{seed}', 'seed': seed} for seed in seeds(args)]


def match_command(args, out, seed):
    minutes = int(args.eliminated_minutes) + 1
    return [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
            '--output-dir', str(out), '--skirmish-ffa-size', str(args.size),
            '--skirmish-map-seed', str(seed), '--seed', str(seed), '--mod', 'dunecity',
            '--game-options', str(root / 'tests/performance/data/stefan-game-options.ini'),
            '--house', HOUSES[0], '--match-sides', ','.join(f'{h}:default' for h in HOUSES),
            '--local-spectator', '--enemy-difficulty', 'brutal', '--partner-difficulty', 'brutal',
            '--minutes', str(minutes), '--capture-mib', '256', '--wall-timeout', str(args.wall_timeout),
            '--path-workers', '1', '--no-ai-telemetry', '--army-deploys-regression-observer']


def build_probe(args, scratch):
    """The balance probe every seed reuses: the first seed's own command, prepared only."""
    out = Path(scratch) / 'probe'
    remove_tree(out)
    subprocess.run(match_command(args, out, seeds(args)[0]) + ['--build-only'], cwd=root, check=True,
                   capture_output=True, text=True)
    remove_tree(out / 'profile')
    return probe_binary(out)


def play(args, scratch, binary, game):
    """One match; appends its games.csv row and writes ffa-<seed>.log (the lines the checks read)."""
    seed = game['seed']
    out = Path(scratch) / game['id']
    remove_tree(out)
    started = time.time()
    outcome = subprocess.run(match_command(args, out, seed) + ['--prebuilt-probe', str(binary)],
                             cwd=root, capture_output=True, text=True)
    log = out / 'run.log'
    error = ''
    if outcome.returncode != 0 or not log.exists():
        error = f'match did not run (rc={outcome.returncode})'
        print(f'seed {seed}:', outcome.stdout[-3000:], outcome.stderr[-3000:])
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    kept = [line[line.index(marker):] for line in lines for marker in MARKERS if marker in line]
    (results / f'{game["id"]}.log').write_text(''.join(line + '\n' for line in kept))
    row = {'id': game['id'], 'seed': seed, 'wall_s': round(time.time() - started), 'error': error}
    path = results / 'games.csv'
    exists = path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if not exists:
            writer.writeheader()
        writer.writerow(row)
    finish_game(scratch, out, bool(error))
    return row


def deploy_failures(args, lines):
    waves = [parse(l, 'FORMATION_WAVE:') for l in lines if 'FORMATION_WAVE:' in l]
    army = [parse(l, 'FFA_ARMY_SAMPLE:') for l in lines if 'FFA_ARMY_SAMPLE:' in l]
    failures = []

    early = [w for w in waves if int(w['launched']) <= args.wave_minutes * CYCLES_PER_MINUTE]
    biggest = max((int(w['members']) for w in early), default=0)
    print(f'waves: {len(waves)} in all, {len(early)} by minute {args.wave_minutes:g}, largest of those {biggest} units; '
          f'first at minute {min((int(w["launched"]) for w in waves), default=0) / CYCLES_PER_MINUTE:.1f}')
    if biggest < args.wave_members:
        failures.append(f'no wave of {args.wave_members}+ units by minute {args.wave_minutes:g}')

    per_house = defaultdict(list)
    for sample in army:
        per_house[sample['house']].append(sample)
    if not army:
        failures.append('reserve measurement unavailable: no FFA_ARMY_SAMPLE rows')
    for house, samples in sorted(per_house.items()):
        outside = sum(int(s['outside_value']) for s in samples) / max(1, sum(int(s['army_value']) for s in samples))
        garrisoned_rows = [s for s in samples if s.get('garrisoned') in ('0', '1')]
        garrisoned_text = (str(sum(s['garrisoned'] == '1' for s in garrisoned_rows))
                           if garrisoned_rows else 'n/a (unmeasured)')
        position = f'army outside its base {outside:.0%}, garrisoned in {garrisoned_text}'
        if any(s.get('roles_measured') == '0' or
               any(s.get(f) in ('', None) or int(s[f]) < 0
                   for f in ('free_value', 'free_inside_value')) for s in samples):
            print(f'house {house}: {len(samples)} minutes sampled, reserve n/a (unmeasured), {position}')
            failures.append(f'house {house} reserve measurement unavailable')
            continue
        best = run = 0
        inside_minutes = 0
        for sample in sorted(samples, key=lambda s: int(s['minute'])):
            free = int(sample['free_value'])
            inside = free >= args.reserve_value and int(sample['free_inside_value']) * 10 >= free * 8
            inside_minutes += inside
            run = run + 1 if inside else 0
            best = max(best, run)
        print(f'house {house}: {len(samples)} minutes sampled, reserve 80%+ inside its base in {inside_minutes} '
              f'(longest stretch {best}), {position}')
        if best > args.inside_minutes:
            failures.append(f'house {house} kept its reserve inside its base for {best} minutes in a row')
    return failures


def first_elimination(lines):
    eliminated = [parse(l, 'VANILLA_MATCH_ELIMINATED:') for l in lines if 'VANILLA_MATCH_ELIMINATED:' in l]
    print('  eliminations: ' + (', '.join(f'house {e["house"]} at {float(e["end_s"]) / 60:.1f} min'
                                         for e in eliminated) or 'none'))
    return min((float(e['end_s']) / 60 for e in eliminated), default=None)


def report(args):
    """Check the collected seeds; returns the failures (empty when the gate passes)."""
    results = Path(args.results)
    latest = {row['id']: row for row in read(results / 'games.csv')}
    games = schedule(args)
    missing = [g['seed'] for g in games if g['id'] not in latest or latest[g['id']]['error']]
    if missing:
        print('FAIL: a match did not run (seeds ' + ', '.join(map(str, missing)) + ')')
        return [f'seeds {missing} did not run']
    logs = [(results / f'{g["id"]}.log').read_text(errors='replace').splitlines() for g in games]

    print(f'seed {games[0]["seed"]} (deploy checks):')
    failures = deploy_failures(args, logs[0])

    finished = []
    for game, lines in zip(games, logs):
        print(f'seed {game["seed"]}:')
        first = first_elimination(lines)
        if first is not None and first <= args.eliminated_minutes:
            finished.append(game['seed'])
    print(f'seeds with a house eliminated by minute {args.eliminated_minutes:g}: '
          f'{", ".join(map(str, finished)) or "none"} of {", ".join(str(g["seed"]) for g in games)}')
    if not finished:
        failures.append(f'nobody eliminated by minute {args.eliminated_minutes:g} on any seed')

    for failure in failures:
        print('FAIL: ' + failure)
    if not failures:
        print('PASS: the FFA armies deployed, launched waves and eliminated a house')
    return failures


def run(args):
    results = Path(args.results)
    if args.fresh:
        shutil.rmtree(results, ignore_errors=True)
    results.mkdir(parents=True, exist_ok=True)
    done = {row['id'] for row in read(results / 'games.csv') if not row['error']}
    todo = [game for game in schedule(args) if game['id'] not in done]
    if todo:
        require_free_space()
        with scratch_dir('ffa-army-deploys', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max(1, args.jobs)) as pool:
                list(pool.map(lambda game: play(args, scratch, binary, game), todo))
    return 1 if report(args) else 0


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--results', '--output-dir', dest='results', type=Path, required=True,
                    help='games.csv and ffa-<seed>.log; seeds already in games.csv are not replayed')
parser.add_argument('--fresh', action='store_true', help='Delete --results first')
parser.add_argument('--scratch', type=Path)
parser.add_argument('--size', type=int, default=160)
parser.add_argument('--seeds', default='9101,9103,9105',
                    help='Map and match seeds; the first also gets the deploy checks')
parser.add_argument('--jobs', type=int, default=3)
parser.add_argument('--wave-minutes', type=float, default=8.0)
parser.add_argument('--wave-members', type=int, default=30)
parser.add_argument('--reserve-value', type=int, default=5000)
parser.add_argument('--inside-minutes', type=int, default=6)
parser.add_argument('--eliminated-minutes', type=float, default=35.0)
parser.add_argument('--wall-timeout', type=int, default=1500)
parser.add_argument('--report', action='store_true', help='Only check the games already in --results')
args = parser.parse_args()
if __name__ == '__main__':
    sys.exit((1 if report(args) else 0) if args.report else run(args))
