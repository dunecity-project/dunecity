#!/usr/bin/env python3
"""Summarise DuneCity campaign playtests (scenario-matrix.py --categories citycampaign).

    python3 tests/ai/city-campaign-report.py RESULTS [RESULTS ...]

One row per mission and helper/enemy pair: wins / games, how they were won (objectives,
survival, conquest), losses, time-limit games and the median end minute. docs/city-campaign.md
holds the targets: the helper must beat enemies one tier lower, and the win rate should fall
gently from mission 1 to mission 10.
"""
import csv
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

PAIRS = ('helper-brutal-v-hard', 'helper-hard-v-medium', 'helper-medium-v-easy')


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
    games = rows(sys.argv[1:])
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


if __name__ == '__main__':
    main()
