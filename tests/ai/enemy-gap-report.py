#!/usr/bin/env python3
"""How close QuantBot builds to enemy buildings (protocol 141, "build right up to enemy buildings").

Reads placements.csv written by tests/ai/vanilla-match.py (one row per building a QuantBot placed,
slabs and roads excluded; enemy_gap = clear tiles to the nearest enemy building, 0 = touching,
-1 = none standing) and prints, per results directory and label: buildings placed per house-game,
the gap to the nearest enemy building (mean, median, 10th percentile, minimum), the share placed
touching (gap 0) or within 2 and 5 tiles, and the share placed on a threatened site. City zones
(R/C/I lots) and other buildings are shown separately.

    python3 tests/ai/enemy-gap-report.py RESULTS_DIR [RESULTS_DIR ...]
"""
import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

ZONES = {20, 21, 22}  # Structure_ZoneResidential, Commercial, Industrial (include/data.h)


def percentile(values, share):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def summarise(rows, house_games):
    gaps = [int(r['enemy_gap']) for r in rows if int(r['enemy_gap']) >= 0]
    line = {'placed': len(rows), 'per_house_game': len(rows) / house_games if house_games else 0.0}
    if gaps:
        line.update(mean=statistics.mean(gaps), median=statistics.median(gaps), p10=percentile(gaps, 0.10),
                    min=min(gaps), touching=sum(g == 0 for g in gaps) / len(gaps),
                    within2=sum(g <= 2 for g in gaps) / len(gaps), within5=sum(g <= 5 for g in gaps) / len(gaps))
    line['threatened'] = sum(int(r['threat'] or 0) > 0 for r in rows) / len(rows) if rows else 0.0
    line['tolerated'] = sum(r['tolerated'] == '1' for r in rows) / len(rows) if rows else 0.0
    return line


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('results', nargs='+', type=Path)
    args = parser.parse_args()
    header = ('results', 'label', 'kind', 'house_games', 'placed', 'per_house_game', 'mean', 'median', 'p10', 'min',
              'touching', 'within2', 'within5', 'threatened', 'tolerated')
    print('\t'.join(header))
    for results in args.results:
        with (results / 'placements.csv').open(newline='') as handle:
            rows = list(csv.DictReader(handle))
        by_label = defaultdict(list)
        house_games = defaultdict(set)
        for row in rows:
            label = row['label'] or row['tag']
            by_label[label].append(row)
            house_games[label].add((row['map'], row['seed'], row['sides'], row['house']))
        for label in sorted(by_label):
            for kind, keep in (('buildings', lambda r: int(r['item']) not in ZONES),
                               ('zones', lambda r: int(r['item']) in ZONES)):
                line = summarise([r for r in by_label[label] if keep(r)], len(house_games[label]))
                cells = [str(results), label, kind, str(len(house_games[label]))]
                for field in header[4:]:
                    value = line.get(field, '')
                    cells.append(f'{value:.3f}' if isinstance(value, float) else str(value))
                print('\t'.join(cells))


if __name__ == '__main__':
    main()
