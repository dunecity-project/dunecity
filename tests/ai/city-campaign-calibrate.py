#!/usr/bin/env python3
"""Fit each DuneCity campaign mission's enemy strength per difficulty from playtest games.

docs/city-campaign.md, "Enemy strength per mission". Reads the games.csv of scenario-matrix
--categories citycampaign runs (any mix of --city-strength grids and plain validations). Each game's
strength is its -sS multiplier times the [DIFFICULTY] value the generator held at the game's source
commit; games whose mission had a different grace (first_attack) at that commit than the generator
has now are left out.

  python3 tests/ai/city-campaign-calibrate.py RESULTS[:1,2,4]...          fit and print the values
  python3 tests/ai/city-campaign-calibrate.py RESULTS... --eval           predictions at today's values
  python3 tests/ai/city-campaign-calibrate.py RESULTS... --write          write the fitted values into
                                                                           scripts/gen_city_campaign.py

Model per pairing: logit P(helper wins) = a[m] + (b + d[m]) ln V, ridge-shrunk intercepts and
per-mission slope offsets. Targets (mission m = 1..10):
  one tier up  (Medium v Easy, Hard v Medium, Brutal v Hard): T1(m) = 0.85 falling to 0.55
  same tier    (Medium v Medium, Hard v Hard, Brutal v Brutal): at most T0(m) = 0.35 falling to 0.25
Medium and Hard sit where the same-tier helper wins <= 35% and the one-tier-up helper >= 55%, aimed
at T1(m); with no such strength the same-tier rule (<= 40%) wins (Stefan, 2026-11-19). Easy: Medium v
Easy at T1(m). Brutal: Brutal v Brutal at T0(m). Strengths are searched only within the range the
games played for that mission and enemy tier (no extrapolation) and rounded to 0.05.
"""
import argparse
import csv
import functools
import json
import math
import pathlib
import re
import subprocess

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = 'scripts/gen_city_campaign.py'
TIERS = ('Easy', 'Medium', 'Hard', 'Brutal')
PAIRS = (('MvE', 'medium-v-easy', 'Easy'), ('MvM', 'medium-v-medium', 'Medium'),
         ('HvM', 'hard-v-medium', 'Medium'), ('HvH', 'hard-v-hard', 'Hard'),
         ('BvH', 'brutal-v-hard', 'Hard'), ('BvB', 'brutal-v-brutal', 'Brutal'))
GAME_ID = re.compile(r'citycampaign/helper-([a-z]+-v-[a-z]+)(?:-s([0-9.]+))?/dunecity/atreides-m(\d+)/')
MISSION = re.compile(r'Mission\(\s*\n\s*(\d+),')


def parse_missions(text):
    """mission -> (first_attack, {tier: value}) from the generator source."""
    out, starts = {}, list(MISSION.finditer(text))
    for i, m in enumerate(starts):
        block = text[m.end():starts[i + 1].start() if i + 1 < len(starts) else len(text)]
        grace = re.search(r'first_attack=(\d+)', block)
        diff = re.search(r'difficulty=\{([^}]*)\}', block)
        values = {k: float(v) for k, v in re.findall(r'"(\w+)":\s*([0-9.]+)', diff.group(1))} if diff else {}
        out[int(m.group(1))] = (int(grace.group(1)) if grace else 0, values)
    return out


@functools.lru_cache(maxsize=None)
def missions_at(commit):
    text = subprocess.run(['git', '-C', str(ROOT), 'show', f'{commit}:{GENERATOR}'],
                          check=True, capture_output=True, text=True).stdout
    return parse_missions(text)


def t1(m): return 0.85 - 0.30 * (m - 1) / 9
def t0(m): return 0.35 - 0.10 * (m - 1) / 9


def fit(rows, ridge=0.5, slope_ridge=1.0):
    x = np.zeros((len(rows), 21)); y = np.zeros(len(rows))
    for i, (m, lv, won) in enumerate(rows):
        x[i, m - 1] = 1; x[i, 10] = lv; x[i, 10 + m] = lv; y[i] = won
    beta = np.zeros(21)
    reg = np.diag([ridge] * 10 + [1e-3] + [slope_ridge] * 10)
    for _ in range(200):
        p = 1 / (1 + np.exp(-np.clip(x @ beta, -30, 30)))
        step = np.clip(np.linalg.solve((x * (p * (1 - p))[:, None]).T @ x + reg, x.T @ (y - p) - reg @ beta), -5, 5)
        beta += step
        if np.abs(step).max() < 1e-8:
            break
    return beta


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('results', nargs='+', help='scenario-matrix results directories, optionally DIR:1,2,4')
    ap.add_argument('--eval', action='store_true', help='print predictions at the generator\'s values')
    ap.add_argument('--write', action='store_true', help='write the fitted values into the generator')
    args = ap.parse_args()

    current = parse_missions((ROOT / GENERATOR).read_text())
    obs, played, skipped = {}, {}, 0
    for spec in args.results:
        directory, _, only = spec.partition(':')
        keep = {int(x) for x in only.split(',')} if only else None
        for row in csv.DictReader(open(pathlib.Path(directory) / 'games.csv')):
            match = GAME_ID.match(row['id'])
            if not match or row['outcome'] not in ('won', 'lost', 'time_limit'):
                continue
            pair, mission = match.group(1), int(match.group(3))
            if keep is not None and mission not in keep:
                continue
            grace, values = missions_at(row['source_commit'])[mission]
            if grace != current[mission][0]:
                skipped += 1
                continue
            value = float(match.group(2) or 1) * values[pair.split('-v-')[1].capitalize()]
            span = played.setdefault((mission, pair.split('-v-')[1].capitalize()), [value, value])
            span[0], span[1] = min(span[0], value), max(span[1], value)
            obs.setdefault(pair, []).append((mission, math.log(value), 1.0 if row['outcome'] == 'won' else 0.0))
    print(f'{sum(map(len, obs.values()))} games, {skipped} left out (grace changed since)')
    models = {pair: fit(rows) for pair, rows in obs.items()}

    def prob(pair, m, v):
        beta = models[pair]
        return 1 / (1 + math.exp(-(beta[m - 1] + (beta[10] + beta[10 + m]) * math.log(v))))

    def solve(pair, m, target, lo, hi):
        beta = models[pair]
        a, b = beta[m - 1], beta[10] + beta[10 + m]
        if b >= 0:
            return hi
        return min(hi, max(lo, math.exp((math.log(target / (1 - target)) - a) / b)))

    fitted = {}
    for m in range(1, 11):
        bounds = {t: tuple(played[(m, t)]) for t in TIERS}

        def window(lower, same, tier):
            lo, hi = bounds[tier]
            s0, s1 = solve(same, m, 0.40, lo, hi), solve(lower, m, 0.50, lo, hi)
            m0, m1 = solve(same, m, 0.35, lo, hi), solve(lower, m, 0.55, lo, hi)
            if m0 <= m1:
                return min(max(solve(lower, m, t1(m), lo, hi), m0), m1)
            return math.sqrt(s0 * s1) if s0 <= s1 else s0

        raw = {'Easy': solve('medium-v-easy', m, t1(m), *bounds['Easy']),
               'Medium': window('hard-v-medium', 'medium-v-medium', 'Medium'),
               'Hard': window('brutal-v-hard', 'hard-v-hard', 'Hard'),
               'Brutal': solve('brutal-v-brutal', m, t0(m), *bounds['Brutal'])}
        fitted[m] = {t: max(0.25, round(raw[t] * 20) / 20) for t in TIERS}

    def report(values, title):
        print(title)
        total = {k: 0.0 for k, _, _ in PAIRS}
        for m in range(1, 11):
            row = {k: prob(p, m, values[m][t]) for k, p, t in PAIRS}
            for k in total:
                total[k] += row[k] / 10
            flags = [k for k in ('MvM', 'HvH', 'BvB') if row[k] > 0.40] + [k for k in ('MvE', 'HvM', 'BvH') if row[k] < 0.50]
            print(f'M{m:<2} ' + ' '.join(f'{t[0]} {values[m][t]:<5g}' for t in TIERS) + ' | '
                  + ' '.join(f'{k} {v:.2f}' for k, v in row.items()) + (f'  MISS {" ".join(flags)}' if flags else ''))
        print('all ' + ' ' * 36 + '| ' + ' '.join(f'{k} {v:.2f}' for k, v in total.items()))

    if args.eval:
        report({m: current[m][1] for m in range(1, 11)}, 'predicted at the generator\'s values:')
    report(fitted, 'fitted values:')
    print(json.dumps({str(m): v for m, v in fitted.items()}))
    if args.write:
        text = (ROOT / GENERATOR).read_text()
        out, pos = [], 0
        for match in MISSION.finditer(text):
            d = re.compile(r'difficulty=\{[^}]*\}').search(text, match.end())
            v = fitted[int(match.group(1))]
            out += [text[pos:d.start()], 'difficulty={' + ', '.join(f'"{t}": {v[t]:g}' for t in TIERS) + '}']
            pos = d.end()
        (ROOT / GENERATOR).write_text(''.join(out) + text[pos:])
        print(f'wrote {GENERATOR}; regenerate the maps with python3 {GENERATOR}')


if __name__ == '__main__':
    main()
