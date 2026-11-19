#!/usr/bin/env python3
"""Summarise DuneCity campaign playtests (scenario-matrix.py --categories citycampaign).

    python3 tests/ai/city-campaign-report.py RESULTS [RESULTS ...]

One row per mission and helper/enemy pair (--equal: the same-tier pairings, where the helper
must lose at least 60%): wins / games, how they were won (objectives,
survival, conquest), losses, time-limit games and the median end minute. docs/city-campaign.md
holds the targets: the helper must beat enemies one tier lower, and the win rate should fall
gently from mission 1 to mission 10.
"""
import csv
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

LOWER = ('helper-brutal-v-hard', 'helper-hard-v-medium', 'helper-medium-v-easy')
EQUAL = ('helper-brutal-v-brutal', 'helper-hard-v-hard', 'helper-medium-v-medium')
PAIRS = EQUAL if '--equal' in sys.argv else LOWER


def rows(dirs):
    seen = {}
    for d in dirs:
        path = Path(d) / 'games.csv'
        if not path.exists():
            continue
        with path.open() as handle:
            for row in csv.DictReader(handle):
                if row['category'] == 'citycampaign':
                    seen[row['id']] = row
    return list(seen.values())


def main():
    games = rows([a for a in sys.argv[1:] if not a.startswith('--')])
    table = defaultdict(list)
    for row in games:
        mission = int(re.search(r'-m(\d+)/', row['id']).group(1))
        pair = row['cell'].split('#')[0].split('/')[0]
        table[(mission, pair)].append(row)
    print(f'{"mission":>7}  ' + '  '.join(f'{p[7:]:>26}' for p in PAIRS))
    totals = defaultdict(lambda: [0, 0])
    for mission in range(1, 11):
        cells = []
        for pair in PAIRS:
            group = table.get((mission, pair), [])
            if not group:
                cells.append(f'{"-":>26}')
                continue
            won = [r for r in group if r['outcome'] == 'won']
            lost = sum(r['outcome'] == 'lost' for r in group)
            limit = sum(r['outcome'] not in ('won', 'lost') for r in group)
            errors = sum(bool(r['error']) for r in group)
            kinds = defaultdict(int)
            for r in won:
                kinds[{'objectives': 'o', 'survival': 's', 'conquest': 'c', 'quota': 'q'}.get(r['win_type'], '?')] += 1
            minutes = [float(r['minutes']) for r in won if r['minutes']]
            med = f'{statistics.median(minutes):.0f}m' if minutes else '-'
            kind = ''.join(f'{v}{k}' for k, v in sorted(kinds.items()))
            text = f'{len(won)}/{len(group)} {kind} L{lost} T{limit}{" E" + str(errors) if errors else ""} {med}'
            cells.append(f'{text:>26}')
            totals[pair][0] += len(won)
            totals[pair][1] += len(group)
        print(f'{mission:>7}  ' + '  '.join(cells))
    print(f'{"total":>7}  ' + '  '.join(f'{f"{totals[p][0]}/{totals[p][1]}":>26}' for p in PAIRS))
    print('\nwins: o objectives, s survival, c conquest; L lost, T time limit, E errors; median winning minute')
    if '--markdown' in sys.argv:
        print('\n| Mission | ' + ' | '.join(p[7:].replace('-v-', ' v ') for p in PAIRS) + ' | All |')
        print('|---' * (len(PAIRS) + 2) + '|')
        for mission in range(1, 11):
            cells, won_all, n_all = [], 0, 0
            for pair in PAIRS:
                group = table.get((mission, pair), [])
                won = sum(r['outcome'] == 'won' for r in group)
                won_all += won
                n_all += len(group)
                cells.append(f'{won}/{len(group)}' if group else '-')
            share = f'{100 * won_all // n_all}%' if n_all else '-'
            print(f'| {mission} | ' + ' | '.join(cells) + f' | {share} |')
    if '--detail' in sys.argv:
        print('\nper game: mission pair seed outcome minute | objective samples minute:[population,credits,crime,land,enemies]')
        for row in sorted(games, key=lambda r: r['id']):
            mission = int(re.search(r'-m(\d+)/', row['id']).group(1))
            pair = row['cell'].split('#')[0].split('/')[0][7:]
            samples = json.loads(row.get('objectives') or '{}')
            last = sorted(samples.items(), key=lambda kv: int(kv[0]))
            trace = ' '.join(f'{k}:{v}' for k, v in last[-4:])
            print(f'M{mission:02d} {pair:14} {row["seed"]:>9} {row["outcome"] or row["error"]:10} {row["minutes"]:>6} | {trace}')


if __name__ == '__main__':
    main()
