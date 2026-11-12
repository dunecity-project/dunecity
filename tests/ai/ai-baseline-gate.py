#!/usr/bin/env python3
"""1.0.807 promotion check (docs/ai-baseline-gate.md); Python 3.9 standard library.

On 1.0.807's own deck the candidate must win at least as many games as the
switch-OFF arm of the same source. 1.0.807's wins are printed for information.
Every house in a deck game runs that build's AI, so a game is won when it ends
with a winner (outcome "decided") before the deck's 40-minute cap.
"""
import argparse
import csv
import json
from pathlib import Path
import sys

import ai_baseline_scorecard as scorecard

RELEASE_807 = 'abdf71e70697a01cae2ba27ed86aa9bfc2103e97'
BRIDGE_807 = '863d1ac66c6604ceef3d73553588eb43882d5940'
SEEDS_807 = frozenset((9101, 148680876, 55001, 77001, 1841712204, 8101, 8202, 8303))
FIXTURE_MANIFEST = Path(__file__).with_name('baseline-fixtures') / 'manifest.json'
DECK_FIELDS = ('mod', 'difficulty', 'seed', 'research', 'map_sha256', 'options_sha256')


def games(path):
    path = Path(path)
    with open(path / 'games.csv' if path.is_dir() else path, newline='') as stream:
        return {row['id']: row for row in csv.DictReader(stream)}


def wins(rows):
    return sum(row['outcome'] == 'decided' for row in rows.values())


def observation_reference(path):
    """Read the canonical export's deck identities and native wins, never synthetic raw fields."""
    manifest = json.loads(FIXTURE_MANIFEST.read_text(encoding='utf-8'))
    selected = [game for game in manifest['games']
                if game.get('research', 'off') == 'off' and game['seed'] in SEEDS_807]
    expected = {game['id']: game for game in selected}
    if len(selected) != 192 or len(expected) != 192:
        raise ValueError('1.0.807 fixtures do not identify exactly 192 distinct reference games')
    references, identities, native_wins, seen = {}, {}, {}, set()
    identity_fields = (*DECK_FIELDS, 'source_commit', 'baseline_release_source',
                       'context_sha256', 'content_hash', 'object_data_hash')
    schema_fields = set(scorecard.COLUMNS)
    valid_minutes = {str(value) for value in (-1, *scorecard.CHECKPOINTS)}
    for row in scorecard._csv_rows(path, required=True):
        if set(row) != schema_fields or row['schema_version'] != str(scorecard.SCHEMA_VERSION):
            raise ValueError('1.0.807 observations have an invalid schema')
        if (row['release'] != '1.0.807' or row['arm'] != 'off' or row['research'] != 'off'
                or row['source_commit'] != BRIDGE_807 or row['baseline_release_source'] != RELEASE_807):
            raise ValueError('1.0.807 observations have mismatched release/arm/source provenance')
        key = row['id']
        if key not in expected:
            raise ValueError('1.0.807 observations contain an unexpected context: ' + key)
        game = expected[key]
        fields = dict(mod=game['mod'], difficulty=game['difficulty'], seed=str(game['seed']),
                      research='off', map_sha256=game['map_sha256'],
                      options_sha256=manifest['off_options_by_mod'][game['mod']]['sha256'])
        if any(row[field] != value for field, value in fields.items()):
            raise ValueError('1.0.807 observations have mismatched deck fields: ' + key)
        if row['context_sha256'] != scorecard.context_sha256(
                key, game['mod'], game['difficulty'], 'off', game['seed']):
            raise ValueError('1.0.807 observations have a mismatched context hash: ' + key)
        roster = {str(house): str(team) for house, team in game['roster']}
        if row['house'] not in roster or row['team'] != roster[row['house']]:
            raise ValueError('1.0.807 observations have an unexpected house/team: ' + key)
        if (not row['metric'] or row['status'] not in ('measured', 'censored', 'not_applicable', 'unavailable')
                or row['minute'] not in valid_minutes):
            raise ValueError('1.0.807 observations have malformed metric metadata: ' + key)
        for field in ('content_hash', 'object_data_hash'):
            scorecard._hash(row[field], '1.0.807 observation ' + field, (16, 64))
        identity = tuple(row[field] for field in identity_fields)
        previous = identities.setdefault(key, identity)
        if identity != previous:
            raise ValueError('1.0.807 observations have conflicting context provenance: ' + key)
        item = (key, row['house'], row['metric'], row['minute'])
        if item in seen:
            raise ValueError('1.0.807 observations contain a duplicate record: ' + key)
        seen.add(item)
        if key not in references:
            references[key] = {field: row[field] for field in (*DECK_FIELDS, 'baseline_release_source')}
        if row['metric'] == 'win':
            if (row['minute'] != '-1' or row['status'] != 'measured' or row['value'] not in ('0', '1')
                    or row['reason'].split(';', 1)[0] != 'native_winner_team_zero_preserves_no_winner'):
                raise ValueError('1.0.807 observations require measured binary native win summaries: ' + key)
            native_wins.setdefault(key, {})[row['house']] = row['value'] == '1'
    if references.keys() != expected.keys():
        raise ValueError('1.0.807 observations do not cover all 192 reference contexts')
    won = 0
    for key, game in expected.items():
        roster = {str(house): str(team) for house, team in game['roster']}
        houses = native_wins.get(key, {})
        if houses.keys() != roster.keys():
            raise ValueError('1.0.807 observations have an incomplete native win roster: ' + key)
        winning_teams = {roster[house] for house, win in houses.items() if win}
        if len(winning_teams) > 1 or any(win != (roster[house] in winning_teams)
                                         for house, win in houses.items()):
            raise ValueError('1.0.807 observations have ambiguous native winner summaries: ' + key)
        won += bool(winning_teams)
    return references, won


def problems(candidate, off, reference, *, reference_validated=False):
    found = [f'1.0.807 {key}: native source {row["baseline_release_source"]}'
             for key, row in reference.items() if row['baseline_release_source'] != RELEASE_807]
    for name, rows in (('candidate', candidate), ('OFF', off)):
        if rows.keys() != reference.keys():
            found.append(f'{name} did not play the 1.0.807 deck ({len(rows.keys() ^ reference.keys())} games differ)')
            continue
        found += [f'{name} {key}: {field} differs from 1.0.807' for key, row in rows.items()
                  for field in DECK_FIELDS if row[field] != reference[key][field]]
    raw_arms = (('candidate', candidate), ('OFF', off))
    if not reference_validated:
        raw_arms += (('1.0.807', reference),)
    for name, rows in raw_arms:
        found += [f'{name} {key}: {row["error"]}' for key, row in rows.items() if row['error']]
    sources = {row['source_commit'] for row in (*candidate.values(), *off.values())}
    if len(sources) != 1:
        found.append('candidate and OFF must come from one source commit: ' + ' '.join(sorted(sources)))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--candidate', required=True, help='ai-baseline-league.py results (or games.csv), switch on')
    parser.add_argument('--off', required=True, help='the same source and deck, switch off')
    reference_input = parser.add_mutually_exclusive_group(required=True)
    reference_input.add_argument('--reference', help='the 1.0.807 league results on that deck')
    reference_input.add_argument('--reference-observations',
                                 help='canonical 1.0.807 scorecard CSV[.gz], with all 192 native win rosters')
    args = parser.parse_args(argv)
    candidate, off = games(args.candidate), games(args.off)
    if args.reference_observations:
        try:
            reference, reference_wins = observation_reference(args.reference_observations)
        except (OSError, ValueError, KeyError, TypeError, csv.Error) as error:
            print('INVALID: ' + str(error))
            return 2
    else:
        reference = games(args.reference)
        reference_wins = wins(reference)
    invalid = problems(candidate, off, reference, reference_validated=bool(args.reference_observations))
    if invalid:
        more = f' (+{len(invalid) - 5} more)' if len(invalid) > 5 else ''
        print('INVALID: ' + '; '.join(invalid[:5]) + more)
        return 2
    candidate_wins, off_wins = wins(candidate), wins(off)
    verdict = 'PASS' if candidate_wins >= off_wins else 'FAIL'
    print(f'{verdict}, candidate {candidate_wins} / OFF {off_wins} / 807 {reference_wins}'
          f' (wins of {len(reference)} games)')
    return 0 if verdict == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
