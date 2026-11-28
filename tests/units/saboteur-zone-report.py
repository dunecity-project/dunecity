#!/usr/bin/env python3
"""In-game assertion for Saboteurs Skip City Zones, over the match logs of real games.

Stefan, 2026-11-28: "also make sabateurs not target r/i/c (not valuable enough) unless it is
garissoned", and "we need assertions during the game to ensure the ai is actually doing the bahaviour
we ask for". Per game (a match log, one game a file, or a game's rows in tests/ai/vanilla-match.py's saboteurs.csv),
from the engine's SABOTEUR_* lines:

  zone targets      SABOTEUR_TARGET lines whose target is a Residential, Commercial or Industrial zone
                    (items 20-22), split into ungarrisoned (garrison=0), garrisoned (garrison>0) and unknown
                    (a log from before the garrison field, protocol 149 or older)
  own ungarrisoned  of those, ungarrisoned zones it chose itself (own=1): must be 0 with the rule on
  violations        SABOTEUR_ZONE_RULE: event=violation lines: must be 0
  skips / drops     SABOTEUR_ZONE_SKIP (an ungarrisoned zone the old rule would have taken) and
                    SABOTEUR_ZONE_DROP (a zone given up when its garrison left): the rule acting
  released / hit    Palace releases and detonations; reach = hit / released

--rule on fails (exit 1) on any game with an own ungarrisoned zone target or a violation line. --rule off
only reports. Prints one row per game and the totals; --json writes them too.
"""
import argparse
import csv
import json
import re
import sys
from pathlib import Path

ZONES = {20, 21, 22}
FIELD = re.compile(r'(\w+)=(-?\d+)')


def fields(line):
    return {k: int(v) for k, v in FIELD.findall(line)}


def scan(path):
    row = {'log': str(path), 'targets': 0, 'zone_targets': 0, 'zone_ungarrisoned': 0, 'zone_garrisoned': 0,
           'zone_unknown': 0, 'own_ungarrisoned': 0, 'violations': 0, 'skips': 0, 'drops': 0, 'released': 0,
           'hits': 0, 'zone_hits': 0, 'zone_hits_ungarrisoned': 0, 'zone_hits_garrisoned': 0, 'lost': 0}
    with open(path, errors='replace') as handle:
        for line in handle:
            if 'SABOTEUR_' not in line:
                continue
            if 'SABOTEUR_TARGET:' in line:
                f = fields(line[line.index('SABOTEUR_TARGET:'):])
                row['targets'] += 1
                if f.get('target_item') in ZONES:
                    row['zone_targets'] += 1
                    garrison = f.get('garrison')
                    if garrison is None or garrison < 0:
                        row['zone_unknown'] += 1
                    elif garrison == 0:
                        row['zone_ungarrisoned'] += 1
                        row['own_ungarrisoned'] += f.get('own', 0)
                    else:
                        row['zone_garrisoned'] += 1
            elif 'SABOTEUR_HIT:' in line:
                f = fields(line[line.index('SABOTEUR_HIT:'):])
                row['hits'] += 1
                if f.get('target_item') in ZONES:
                    row['zone_hits'] += 1
                    if f.get('garrison') == 0:
                        row['zone_hits_ungarrisoned'] += 1
                    elif (f.get('garrison') or 0) > 0:
                        row['zone_hits_garrisoned'] += 1
            elif 'SABOTEUR_RELEASED:' in line:
                row['released'] += 1
            elif 'SABOTEUR_LOST:' in line:
                row['lost'] += 1
            elif 'SABOTEUR_ZONE_RULE: event=violation' in line:
                row['violations'] += 1
            elif 'SABOTEUR_ZONE_SKIP:' in line:
                row['skips'] += 1
            elif 'SABOTEUR_ZONE_DROP:' in line:
                row['drops'] += 1
    row['reach'] = row['hits'] / row['released'] if row['released'] else None
    row['passed'] = row['own_ungarrisoned'] == 0 and row['violations'] == 0
    return row


def scan_csv(path):
    """saboteurs.csv of tests/ai/vanilla-match.py: one row per game (tag, difficulty, map, seed, sides)."""
    games = {}
    with open(path, newline='') as handle:
        for r in csv.DictReader(handle):
            key = (r['tag'], r['difficulty'], r['map'], r['seed'], r['sides'])
            row = games.setdefault(key, {'log': f"{path}:{'/'.join(key)}", 'targets': 0, 'zone_targets': 0,
                                         'zone_ungarrisoned': 0, 'zone_garrisoned': 0, 'zone_unknown': 0, 'own_ungarrisoned': 0,
                                         'violations': 0, 'skips': 0, 'drops': 0, 'released': 0, 'hits': 0,
                                         'zone_hits': 0, 'zone_hits_ungarrisoned': 0, 'zone_hits_garrisoned': 0, 'lost': 0})
            event = r['event']
            item = int(r['target_item']) if r.get('target_item') not in (None, '') else None
            garrison = int(r['garrison']) if r.get('garrison') not in (None, '') else None
            if event == 'target':
                row['targets'] += 1
                if item in ZONES:
                    row['zone_targets'] += 1
                    if garrison is None or garrison < 0:
                        row['zone_unknown'] += 1
                    elif garrison == 0:
                        row['zone_ungarrisoned'] += 1
                        row['own_ungarrisoned'] += int(r.get('own') or 0)
                    else:
                        row['zone_garrisoned'] += 1
            elif event == 'hit':
                row['hits'] += 1
                if item in ZONES:
                    row['zone_hits'] += 1
                    if garrison == 0:
                        row['zone_hits_ungarrisoned'] += 1
                    elif (garrison or 0) > 0:
                        row['zone_hits_garrisoned'] += 1
            elif event in ('released', 'lost'):
                row['released' if event == 'released' else 'lost'] += 1
            elif event == 'zone_violation':
                row['violations'] += 1
            elif event == 'zone_skip':
                row['skips'] += 1
            elif event == 'zone_drop':
                row['drops'] += 1
    for row in games.values():
        row['reach'] = row['hits'] / row['released'] if row['released'] else None
        row['passed'] = row['own_ungarrisoned'] == 0 and row['violations'] == 0
    return list(games.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('logs', nargs='+', type=Path,
                        help='match logs, saboteurs.csv of tests/ai/vanilla-match.py, or folders searched for *.log')
    parser.add_argument('--rule', choices=('on', 'off'), default='on')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    paths = []
    for p in args.logs:
        paths += sorted(p.rglob('*.log')) if p.is_dir() else [p]
    rows = []
    for p in paths:
        rows += scan_csv(p) if p.suffix == '.csv' else [scan(p)]
    rows = [r for r in rows if r['released'] or r['targets']]
    keys = ('released', 'hits', 'lost', 'targets', 'zone_targets', 'zone_ungarrisoned', 'zone_garrisoned', 'zone_unknown',
            'own_ungarrisoned', 'violations', 'skips', 'drops', 'zone_hits', 'zone_hits_ungarrisoned', 'zone_hits_garrisoned')
    total = {k: sum(r[k] for r in rows) for k in keys}
    total['games'] = len(rows)
    total['reach'] = total['hits'] / total['released'] if total['released'] else None
    total['failed_games'] = sum(1 for r in rows if not r['passed'])
    for r in rows:
        reach = f" reach={r['reach']:.3f}" if r['reach'] is not None else ''
        print(('PASS ' if r['passed'] or args.rule == 'off' else 'FAIL ') + ' '.join(f'{k}={r[k]}' for k in keys)
              + reach + ' ' + r['log'])
    print('TOTAL ' + ' '.join(f'{k}={v}' for k, v in total.items()))
    if args.json:
        args.json.write_text(json.dumps({'rule': args.rule, 'total': total, 'games': rows}, indent=1))
    if args.rule == 'on' and total['failed_games']:
        print(f"SABOTEUR_ZONE_ASSERT_FAIL: {total['failed_games']} of {len(rows)} games had a Saboteur choose an "
              "ungarrisoned zone itself", file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
