#!/usr/bin/env python3
"""Carryall manoeuvres: what each arm did (docs/formations.md, "Mass Carryall manoeuvres").

Reads result directories of vanilla-match.py games played as one candidate label against
`default` (`--ffa default@<style>,default` or `--pairs default@<style>:default`) and prints, per
arm, every Carryall-manoeuvre count of the candidate houses per house-game (how often it fired,
units ordered, lifted, landed and lost in the air or waiting, refusals) and the Carryalls built
and lost by the candidate houses against the control houses of the same games. Scores and their
intervals come from the harness itself: vanilla-match.py --report.

    python3 tests/ai/airlift-report.py RESULTS/arm [RESULTS/arm ...] [--csv OUT.csv]
"""
import argparse
import csv
from collections import defaultdict
from pathlib import Path

# The counter that says "this arm's manoeuvre happened" (first match on the style name).
FIRED = (('rear', 'airlift_rear'), ('flank', 'airlift_flank'), ('squad-retreat', 'airlift_squad_retreats'),
         ('retreat', 'airlift_retreats'), ('rescue', 'airlift_rescues'), ('ferry', 'airlift_ferried'),
         ('harvester-raid', 'airlift_harvester_raids'), ('perimeter-raid', 'airlift_perimeter_raids'),
         ('base-defence', 'airlift_base_defence'), ('harvester-defence', 'airlift_harvester_defence'),
         ('defence', 'airlift_base_defence'))
SHARED = ('carryalls', 'carryalls_built', 'carryalls_lost', 'kill_value', 'loss_value')


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def report(directory):
    games = list(csv.DictReader((directory / 'games.csv').open()))
    houses = list(csv.DictReader((directory / 'houses.csv').open()))
    key = [c for c in games[0] if c in houses[0] and c not in ('winner', 'end_s', 'error')]
    by_game = defaultdict(list)
    for row in houses:
        by_game[tuple(row[c] for c in key)].append(row)
    labels = sorted({row['label'] for row in houses})
    candidates = [label for label in labels if label != 'default']
    if len(candidates) != 1 or 'default' not in labels:
        raise SystemExit(f'{directory}: expected one candidate label against default, found {labels}')
    candidate = candidates[0]
    style = candidate.split('@', 1)[-1]
    fired_column = next((column for name, column in FIRED if name in style), None)
    counters = [c for c in houses[0] if c.startswith('airlift_')]
    totals = {candidate: defaultdict(float), 'default': defaultdict(float)}
    house_games = {candidate: 0, 'default': 0}
    fired = played = errors = 0
    for game in games:
        rows = by_game.get(tuple(game[c] for c in key), [])
        if game.get('error') or len(rows) < 2:
            errors += 1
            continue
        played += 1
        for row in rows:
            for column in counters + list(SHARED):
                totals[row['label']][column] += number(row.get(column))
            house_games[row['label']] += 1
            if row['label'] == candidate and fired_column and number(row.get(fired_column)) > 0:
                fired += 1
    c, d = totals[candidate], totals['default']
    hg, hd = max(1, house_games[candidate]), max(1, house_games['default'])
    print(f'\n## {candidate}  ({directory}): {played} games, {errors} errors')
    if fired_column:
        print(f'fired ({fired_column}) in {fired}/{house_games[candidate]} candidate house-games '
              f'({100 * fired / hg:.0f}%), {c[fired_column] / hg:.2f} times a house-game')
    shown = [(column, c[column] / hg) for column in counters if c[column]]
    print('per candidate house-game: ' + (', '.join(f'{column[8:]} {value:.2f}' for column, value in shown) or 'nothing'))
    print(f'Carryalls built {c["carryalls_built"] / hg:.1f} v {d["carryalls_built"] / hd:.1f} (control), lost '
          f'{c["carryalls_lost"] / hg:.1f} v {d["carryalls_lost"] / hd:.1f}, at the end '
          f'{c["carryalls"] / hg:.1f} v {d["carryalls"] / hd:.1f}; kill value {c["kill_value"] / hg:.0f} v '
          f'{d["kill_value"] / hd:.0f}, loss value {c["loss_value"] / hg:.0f} v {d["loss_value"] / hd:.0f}')
    row = {'directory': str(directory), 'label': candidate, 'games': played, 'errors': errors,
           'fired_share': round(fired / hg, 4) if fired_column else '', 'house_games': house_games[candidate]}
    row.update({column: round(c[column] / hg, 3) for column in counters})
    row.update({column + '_candidate': round(c[column] / hg, 2) for column in SHARED})
    row.update({column + '_control': round(d[column] / hd, 2) for column in SHARED})
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('results', nargs='+', type=Path)
    parser.add_argument('--csv', type=Path, help='also write one row per arm')
    args = parser.parse_args()
    rows = [report(directory) for directory in args.results]
    if args.csv:
        columns = []
        for row in rows:
            columns += [c for c in row if c not in columns]
        with args.csv.open('w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)


if __name__ == '__main__':
    main()
