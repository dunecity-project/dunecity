#!/usr/bin/env python3
"""1.0.807 promotion check (docs/ai-baseline-gate.md); Python 3.9 standard library.

On 1.0.807's own deck the candidate must win at least as many games as the
switch-OFF arm of the same source. 1.0.807's wins are printed for information.
Every house in a deck game runs that build's AI, so a game is won when it ends
with a winner (outcome "decided") before the deck's 40-minute cap.
"""
import argparse
import csv
from pathlib import Path
import sys

RELEASE_807 = 'abdf71e70697a01cae2ba27ed86aa9bfc2103e97'
DECK_FIELDS = ('mod', 'difficulty', 'seed', 'research', 'map_sha256', 'options_sha256')


def games(path):
    path = Path(path)
    with open(path / 'games.csv' if path.is_dir() else path, newline='') as stream:
        return {row['id']: row for row in csv.DictReader(stream)}


def wins(rows):
    return sum(row['outcome'] == 'decided' for row in rows.values())


def problems(candidate, off, reference):
    found = [f'1.0.807 {key}: native source {row["baseline_release_source"]}'
             for key, row in reference.items() if row['baseline_release_source'] != RELEASE_807]
    for name, rows in (('candidate', candidate), ('OFF', off)):
        if rows.keys() != reference.keys():
            found.append(f'{name} did not play the 1.0.807 deck ({len(rows.keys() ^ reference.keys())} games differ)')
            continue
        found += [f'{name} {key}: {field} differs from 1.0.807' for key, row in rows.items()
                  for field in DECK_FIELDS if row[field] != reference[key][field]]
    for name, rows in (('candidate', candidate), ('OFF', off), ('1.0.807', reference)):
        found += [f'{name} {key}: {row["error"]}' for key, row in rows.items() if row['error']]
    sources = {row['source_commit'] for row in (*candidate.values(), *off.values())}
    if len(sources) != 1:
        found.append('candidate and OFF must come from one source commit: ' + ' '.join(sorted(sources)))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--candidate', required=True, help='ai-baseline-league.py results (or games.csv), switch on')
    parser.add_argument('--off', required=True, help='the same source and deck, switch off')
    parser.add_argument('--reference', required=True, help='the 1.0.807 league results on that deck')
    args = parser.parse_args(argv)
    candidate, off, reference = games(args.candidate), games(args.off), games(args.reference)
    invalid = problems(candidate, off, reference)
    if invalid:
        more = f' (+{len(invalid) - 5} more)' if len(invalid) > 5 else ''
        print('INVALID: ' + '; '.join(invalid[:5]) + more)
        return 2
    candidate_wins, off_wins = wins(candidate), wins(off)
    verdict = 'PASS' if candidate_wins >= off_wins else 'FAIL'
    print(f'{verdict}, candidate {candidate_wins} / OFF {off_wins} / 807 {wins(reference)}'
          f' (wins of {len(reference)} games)')
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
