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

Why the elimination check spans three seeds: whether one particular FFA has eliminated
somebody by minute 30 is chance. Any change that touches the match at all reorders it: about a
third of seeds do (8-9 of 24 in the 1.0.848 sweep). The city-airplane change
(fix/city-airplane, 2026-10-22) moved seed 9101 from 26.9 minutes to no elimination in 45, although
the 12-seed sweep 9101-9112 showed no shift: 6 of 12 seeds eliminated by minute 30 with planes shot
down as before, 5 of 12 with the change (~/Documents/projects/dunecity-results/city-airplane/ffa-sweep).
A single seed only re-pins chance. The 1.0.847 stuck army still fails every seed, because its waves
never leave, so at least one of 9101, 9103 and 9105 has to finish someone; each did in both builds
of that sweep except 9101 with the change.

  run-ffa-army-deploys.py --build-dir build --output-dir build/ffa-army-deploys
"""
import argparse
import subprocess
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

root = Path(__file__).resolve().parents[2]
HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar')
CYCLES_PER_MINUTE = 3750


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def play(args, seed):
    """One match; returns its run.log lines, or None when it did not run."""
    out = args.output_dir / f'seed-{seed}'
    minutes = int(args.eliminated_minutes) + 1
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
               '--output-dir', str(out), '--repeatable', '--skirmish-ffa-size', str(args.size),
               '--skirmish-map-seed', str(seed), '--seed', str(seed), '--mod', 'dunecity',
               '--game-options', str(root / 'tests/performance/data/stefan-game-options.ini'),
               '--house', HOUSES[0], '--match-sides', ','.join(f'{h}:default' for h in HOUSES),
               '--local-spectator', '--enemy-difficulty', 'brutal', '--partner-difficulty', 'brutal',
               '--minutes', str(minutes), '--capture-mib', '256', '--wall-timeout', str(args.wall_timeout),
               '--path-workers', '1', '--no-ai-telemetry']
    outcome = subprocess.run(command, cwd=root, capture_output=True, text=True)
    runs = sorted(out.glob('run-*'), key=lambda p: p.stat().st_mtime)
    log = runs[-1] / 'run.log' if runs else None
    if outcome.returncode != 0 or not log or not log.exists():
        print(f'seed {seed}:', outcome.stdout[-3000:], outcome.stderr[-3000:])
        return None
    return log.read_text(errors='replace').splitlines()


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
    for house, samples in sorted(per_house.items()):
        best = run = 0
        inside_minutes = 0
        for sample in sorted(samples, key=lambda s: int(s['minute'])):
            free = int(sample['free_value'])
            inside = free >= args.reserve_value and int(sample['free_inside_value']) * 10 >= free * 8
            inside_minutes += inside
            run = run + 1 if inside else 0
            best = max(best, run)
        outside = sum(int(s['outside_value']) for s in samples) / max(1, sum(int(s['army_value']) for s in samples))
        print(f'house {house}: {len(samples)} minutes sampled, reserve 80%+ inside its base in {inside_minutes} '
              f'(longest stretch {best}), army outside its base {outside:.0%}, garrisoned in '
              f'{sum(s.get("garrisoned") == "1" for s in samples)}')
        if best > args.inside_minutes:
            failures.append(f'house {house} kept its reserve inside its base for {best} minutes in a row')
    return failures


def first_elimination(lines):
    eliminated = [parse(l, 'VANILLA_MATCH_ELIMINATED:') for l in lines if 'VANILLA_MATCH_ELIMINATED:' in l]
    print('  eliminations: ' + (', '.join(f'house {e["house"]} at {float(e["end_s"]) / 60:.1f} min'
                                         for e in eliminated) or 'none'))
    return min((float(e['end_s']) / 60 for e in eliminated), default=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--size', type=int, default=160)
    parser.add_argument('--seeds', default='9101,9103,9105',
                        help='Map and match seeds; the first also gets the deploy checks')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--wave-minutes', type=float, default=8.0)
    parser.add_argument('--wave-members', type=int, default=30)
    parser.add_argument('--reserve-value', type=int, default=5000)
    parser.add_argument('--inside-minutes', type=int, default=6)
    parser.add_argument('--eliminated-minutes', type=float, default=30.0)
    parser.add_argument('--wall-timeout', type=int, default=1500)
    args = parser.parse_args()
    seeds = [int(s) for s in args.seeds.split(',') if s.strip()]

    with ThreadPoolExecutor(max(1, args.jobs)) as pool:
        logs = list(pool.map(lambda seed: play(args, seed), seeds))
    if any(lines is None for lines in logs):
        print('FAIL: a match did not run (seeds ' + ', '.join(str(s) for s, l in zip(seeds, logs) if l is None) + ')')
        return 1

    print(f'seed {seeds[0]} (deploy checks):')
    failures = deploy_failures(args, logs[0])

    finished = []
    for seed, lines in zip(seeds, logs):
        print(f'seed {seed}:')
        first = first_elimination(lines)
        if first is not None and first <= args.eliminated_minutes:
            finished.append(seed)
    print(f'seeds with a house eliminated by minute {args.eliminated_minutes:g}: '
          f'{", ".join(map(str, finished)) or "none"} of {", ".join(map(str, seeds))}')
    if not finished:
        failures.append(f'nobody eliminated by minute {args.eliminated_minutes:g} on any seed')

    for failure in failures:
        print('FAIL: ' + failure)
    if not failures:
        print('PASS: the FFA armies deployed, launched waves and eliminated a house')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
