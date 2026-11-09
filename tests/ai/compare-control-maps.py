#!/usr/bin/env python3
"""Record a complete native controls receipt, or check a candidate against it.

Controls rule (Stefan 2026-11-09): keep at least --min-kept-ratio (default 75%) of the
native QB/team-1 wins across all 13 controls, including The Sardaukar Outpost (120-minute
limit; its 60-minute receipts cannot qualify). Only maps explicitly outside --in-scope must
stay digest-identical."""
import argparse
import csv
import datetime
import json
from fractions import Fraction
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = '98ca530ab89e47dccbc75f1703ed9e70587a97b8'
IDENTITY = ('id', 'category', 'cell', 'seed', 'mod', 'map', 'sides', 'limit')
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}


def expected_games():
    output = subprocess.check_output(
        [sys.executable, str(ROOT / 'tests/ai/scenario-matrix.py'), '--tier', 'controls', '--list-json'], text=True)
    games = json.loads(output)
    if len(games) != 26:
        raise ValueError(f'Control schedule has {len(games)} games, expected 26')
    return {game['id']: dict(id=game['id'], category=game['category'], cell=game['cell'],
                             seed=str(game['seed']), mod=game['mod'], map=game['map'],
                             sides=' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']),
                             limit=str(game['minutes'])) for game in games}


def validate(rows):
    by_id = {}
    for row in rows:
        gid = row['id']
        if row.get('control_diagnostics') not in (None, '', '0', 0):
            raise ValueError(f'Observed diagnostic game cannot qualify as an unobserved controls gate: {gid}')
        if gid in by_id:
            raise ValueError(f'Duplicate game: {gid}')
        if row.get('error') or not re.fullmatch(r'[0-9a-fA-F]{16}', row.get('digest', '')):
            raise ValueError(f'Failed game or missing final digest: {gid}')
        if row.get('winner_team') not in ('0', '1', '2', '3', '4', '5', '6'):
            raise ValueError(f'Invalid winning team: {gid}')
        if not 0 < float(row['minutes']) <= float(row['limit']) + 0.1:
            raise ValueError(f'Invalid game duration: {gid}')
        if row['winner_team'] == '0' and abs(float(row['minutes']) - float(row['limit'])) > 0.1:
            raise ValueError(f'Draw ended before its {row["limit"]}-minute limit (relabelled limit?): {gid}')
        if not re.fullmatch(r'[0-9a-f]{40}', row.get('source_commit', '')):
            raise ValueError(f'Missing pinned source provenance: {gid}')
        by_id[gid] = row
    wanted = expected_games()
    if set(by_id) != set(wanted):
        raise ValueError(f'Incomplete/wrong controls: missing={sorted(set(wanted) - set(by_id))}; '
                         f'extra={sorted(set(by_id) - set(wanted))}')
    for gid, setup in wanted.items():
        for field in IDENTITY:
            if str(by_id[gid][field]) != setup[field]:
                raise ValueError(f'{gid}: wrong scheduled {field}: {by_id[gid][field]!r} != {setup[field]!r}')
    return by_id


def check_style_seats(rows, *, required):
    """Every row's receipt: 'none' (native) or the one style on the candidate seat only (the first
    side, the Brutal QuantBot). Returns the style ('' for native). Rows played before the receipt
    existed (the recorded baseline) have no field and are accepted only when not required."""
    styles = set()
    for row in rows:
        seats = row.get('style_seats')
        if seats is None or seats == '':
            if required:
                raise ValueError(f'No style-seat receipt (old all-QuantBot harness?): {row["id"]}')
            styles.add('')
            continue
        if seats == 'none':
            styles.add('')
            continue
        house, _, code = row['sides'].split()[0].split(':')
        match = re.fullmatch(r'(\d+)=candidate-([A-Za-z0-9._-]+)', seats)
        if code != 'QB' or not match or int(match.group(1)) != HOUSE_NUMBERS[house]:
            raise ValueError(f'Style on a non-candidate seat ({seats}; candidate {house} QB): {row["id"]}')
        styles.add(match.group(2))
    if len(styles) != 1:
        raise ValueError(f'Mixed styles in one controls run: {sorted(styles)}')
    return styles.pop()


def csv_rows(directory):
    path = Path(directory)
    if path.is_dir():
        path /= 'games.csv'
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def winner(row):
    return f'T{row["winner_team"]} {row["winner"]}' if row['winner_team'] != '0' else 'draw'


def record(args):
    rows = csv_rows(args.results)
    validate(rows)
    if check_style_seats(rows, required=True) != args.native_style:
        raise ValueError('The baseline style-seat receipt does not match --native-style')
    pins = set(args.pin)
    if {row['source_commit'] for row in rows} != pins:
        raise ValueError('Recorded sources do not match --pin')
    if not re.fullmatch(r'[0-9a-f]{40}', args.base):
        raise ValueError('Native engine base must be a full source SHA')
    document = {'schema': 1, 'base': args.base, 'pins': sorted(pins), 'style': 'native',
                'native_style': args.native_style, 'job': args.job,
                'recorded_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'game_count': len(rows), 'games': sorted(rows, key=lambda row: row['id'])}
    Path(args.record).write_text(json.dumps(document, indent=2) + '\n')
    print('| Map | Seed | Native winner | Minutes | Final digest |')
    print('|---|---:|---|---:|---|')
    for row in document['games']:
        print(f'| {row["map"]} | {row["seed"]} | {winner(row)} | {row["minutes"]} | `{row["digest"]}` |')


def compare(args):
    baseline = json.loads(Path(args.baseline).read_text())
    if (baseline.get('schema') != 1 or baseline.get('style') != 'native'
            or not re.fullmatch(r'[0-9a-f]{40}', baseline.get('base', ''))):
        raise ValueError('Not a pinned native controls baseline')
    native = validate(baseline['games'])
    if check_style_seats(baseline['games'], required=False) != baseline.get('native_style', ''):
        raise ValueError('The baseline style-seat receipt does not match its native style')
    rows = csv_rows(args.results)
    candidate = validate(rows)
    style = check_style_seats(rows, required=True)
    print(f'Candidate: {style or "default (no overlay)"} (candidate seat only)')
    problems, lost_wins = [], []
    native_wins = 0
    print('| Map | Seed | Native winner/min | Candidate winner/min | Digest equal | Native win lost |')
    print('|---|---:|---|---|---|---|')
    for gid in sorted(native):
        old, new = native[gid], candidate[gid]
        for field in IDENTITY:
            if old[field] != new[field]:
                problems.append(f'{gid}: changed {field}: {old[field]!r} != {new[field]!r}')
        if old['source_commit'] != new['source_commit']:
            problems.append(f'{gid}: different native/candidate source pins')
        number = int(gid.split('/')[1].split('-')[0])
        equal = old['digest'] == new['digest']
        lost = old['winner_team'] == '1' and new['winner_team'] != '1'
        if old['winner_team'] == '1':
            native_wins += 1
            if lost:
                lost_wins.append(gid)
        if not equal and number not in args.in_scope:
            problems.append(f'{gid}: out-of-scope digest changed')
        print(f'| {old["map"]} | {old["seed"]} | {winner(old)}/{old["minutes"]} | '
              f'{winner(new)}/{new["minutes"]} | {"yes" if equal else "NO"} | {"YES" if lost else "no"} |')
    for gid in lost_wins:
        print(f'LOST: {gid}: native candidate-side win lost')
    kept = native_wins - len(lost_wins)
    kept_enough = kept >= args.min_kept_ratio * native_wins
    rule = f'{float(args.min_kept_ratio):.0%}'
    ratio = f'{kept / native_wins:.3f}' if native_wins else 'n/a'
    print(f'Native QB/team-1 wins on maps 1-13: {native_wins}; kept {kept}; ratio {ratio} (rule >= {rule}): '
          f'{"PASS" if kept_enough else "FAIL"}')
    if not kept_enough:
        problems.append(f'native wins kept {kept}/{native_wins} below {rule}')
    for problem in problems:
        print('FAIL: ' + problem, file=sys.stderr)
    print(f'Controls: {"FAIL" if problems else "PASS"} ({len(candidate)} games; {len(problems)} violations; '
          f'native wins kept {kept}/{native_wins})')
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', required=True, help='Candidate/native results directory or games.csv')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--baseline', help='Compare with this baseline JSON')
    mode.add_argument('--record', help='Write native baseline JSON from complete results')
    parser.add_argument('--base', default=BASE)
    parser.add_argument('--pin', nargs='+', help='Exact source pin(s); mixed harness-only receipts keep each row pin')
    parser.add_argument('--job', help='Native cluster job ID')
    parser.add_argument('--native-style', default='', help='Explicit OFF overlay used as native, if any')
    parser.add_argument('--in-scope', nargs='*', type=int, choices=range(1, 14), default=list(range(1, 14)),
                        help='Map numbers allowed to change digests (default all 13; empty means none)')
    parser.add_argument('--min-kept-ratio', type=Fraction, default=Fraction(3, 4),
                        help='Share of native QB/team-1 wins across all 13 maps the candidate must keep '
                             '(default 0.75; 1 = no native win may be lost)')
    args = parser.parse_args()
    if args.record and (not args.pin or not args.job):
        parser.error('--record requires --pin and --job')
    if not 0 <= args.min_kept_ratio <= 1:
        parser.error('--min-kept-ratio must be between 0 and 1')
    try:
        if args.record:
            record(args)
            return 0
        return compare(args)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'Invalid controls receipt: {error}\n')


if __name__ == '__main__':
    sys.exit(main())
