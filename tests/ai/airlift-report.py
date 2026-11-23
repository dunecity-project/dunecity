#!/usr/bin/env python3
"""Mass Carryall manoeuvres: what each arm did (docs/formations.md, "Mass Carryall manoeuvres").

Reads the result directories of vanilla-match.py free-for-alls played as
`--ffa default@airlift-<arm>,default` (two houses per label in every game) and prints, per arm,
how often the manoeuvre fired, the units it ordered, lifted, landed and lost (in the air, and
waiting for the Carryall), why drops were refused, and the Carryalls built and lost by the
candidate houses against the control houses in the same games. Scores and their intervals come
from the harness itself: vanilla-match.py --report --ffa default@airlift-<arm>,default.

    python3 tests/ai/airlift-report.py RESULTS/rear RESULTS/flank ...
"""
import argparse
import csv
from collections import defaultdict
from pathlib import Path

FIRED = {'rear': 'airlift_rear', 'flank': 'airlift_flank', 'retreat': 'airlift_retreats', 'rescue': 'airlift_rescues',
         'ferry': 'airlift_ferried'}
COUNTS = ('airlift_rear', 'airlift_flank', 'airlift_retreats', 'airlift_retreat_units', 'airlift_rescues',
          'airlift_rescues_damaged', 'airlift_rescues_alone', 'airlift_strikes', 'airlift_ordered', 'airlift_lifted',
          'airlift_landed', 'airlift_lost', 'airlift_lost_waiting', 'airlift_cancelled', 'airlift_no_front',
          'airlift_no_trip', 'airlift_no_carryalls', 'airlift_no_group', 'airlift_no_point', 'airlift_defeat_passes',
          'airlift_retreat_no_carryall', 'airlift_retreat_no_fighter', 'airlift_ferried', 'carryalls', 'carryalls_built',
          'carryalls_lost',
          'kill_value', 'loss_value')


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def load(directory):
    games = list(csv.DictReader((directory / 'games.csv').open()))
    houses = list(csv.DictReader((directory / 'houses.csv').open()))
    key = [c for c in games[0] if c in houses[0] and c not in ('winner', 'end_s', 'error')]
    by_game = defaultdict(list)
    for row in houses:
        by_game[tuple(row[c] for c in key)].append(row)
    return games, by_game, key


def report(directory):
    games, by_game, key = load(directory)
    labels = sorted({row['label'] for rows in by_game.values() for row in rows})
    candidates = [l for l in labels if '@airlift' in l]
    if len(candidates) != 1 or 'default' not in labels:
        raise SystemExit(f'{directory}: expected one default@airlift-* label against default, found {labels}')
    candidate = candidates[0]
    kind = next((k for k in FIRED if candidate.endswith('airlift-' + k) or ('-' + k + '-') in candidate), None)
    errors = 0
    totals = {candidate: defaultdict(float), 'default': defaultdict(float)}
    house_games = {candidate: 0, 'default': 0}
    fired = 0
    played = 0
    for game in games:
        rows = by_game.get(tuple(game[c] for c in key), [])
        if game.get('error') or len(rows) < 2:
            errors += 1
            continue
        played += 1
        for r in rows:
            for c in COUNTS:
                totals[r['label']][c] += number(r.get(c))
            house_games[r['label']] += 1
            if r['label'] == candidate and kind and number(r.get(FIRED[kind])) > 0:
                fired += 1
    print(f'\n## {candidate}  ({directory}): {played} games, {errors} errors')
    c, d = totals[candidate], totals['default']
    hg, hd = max(1, house_games[candidate]), max(1, house_games['default'])
    if kind:
        print(f'fired in {fired}/{house_games[candidate]} candidate house-games ({100 * fired / hg:.0f}%), '
              f'{c[FIRED[kind]] / hg:.2f} times a house-game')
    print(f'per candidate house-game: ordered {c["airlift_ordered"] / hg:.1f}, lifted {c["airlift_lifted"] / hg:.1f}, '
          f'landed {c["airlift_landed"] / hg:.1f}, lost in the air {c["airlift_lost"] / hg:.2f}, '
          f'lost waiting {c["airlift_lost_waiting"] / hg:.2f}, cancelled {c["airlift_cancelled"] / hg:.2f}')
    if kind in ('rear', 'flank'):
        print(f'drops refused (attempts per house-game): no front {c["airlift_no_front"] / hg:.1f}, short trip '
              f'{c["airlift_no_trip"] / hg:.1f}, no two free Carryalls {c["airlift_no_carryalls"] / hg:.1f}, group too small '
              f'{c["airlift_no_group"] / hg:.1f}, no ground {c["airlift_no_point"] / hg:.1f}; strikes '
              f'{c["airlift_strikes"] / hg:.2f}')
    if kind == 'retreat':
        print(f'defeat passes {c["airlift_defeat_passes"] / hg:.1f} a house-game, of which no free Carryall '
              f'{c["airlift_retreat_no_carryall"] / hg:.1f}, nobody under fire {c["airlift_retreat_no_fighter"] / hg:.1f}; '
              f'units flown out {c["airlift_retreat_units"] / hg:.1f}')
    if kind == 'rescue':
        print(f'rescues: damaged {c["airlift_rescues_damaged"] / hg:.1f}, cut off {c["airlift_rescues_alone"] / hg:.1f} a house-game')
    print(f'Carryalls built {c["carryalls_built"] / hg:.1f} v {d["carryalls_built"] / hd:.1f} (control), lost '
          f'{c["carryalls_lost"] / hg:.1f} v {d["carryalls_lost"] / hd:.1f}; kill value {c["kill_value"] / hg:.0f} v '
          f'{d["kill_value"] / hd:.0f}, loss value {c["loss_value"] / hg:.0f} v {d["loss_value"] / hd:.0f}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('results', nargs='+', type=Path)
    args = parser.parse_args()
    for directory in args.results:
        report(directory)


if __name__ == '__main__':
    main()
