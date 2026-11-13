#!/usr/bin/env python3
"""Read campaign partner games one at a time.

Input: scenario-matrix results directories made with --economy-samples --attack-events
(games.csv, samples.csv[.gz], campaign-ai.csv[.gz]). For every game this prints what
happened, from the engine's own CAMPAIGN_AI log lines and per-minute samples: the map
trigger, each opponent's levers and opening, every wave with its cap, the partner's
attacks, a state table every three minutes, and a one-line reading of why it ended.

  campaign-game-report.py RESULTS...                 one line per game, then a table
  campaign-game-report.py RESULTS... --game REGEX    the full story of matching games
"""
import argparse
import collections
import csv
import gzip
import json
import re
import sys
from pathlib import Path

csv.field_size_limit(1 << 30)
CYCLES_PER_MINUTE = 3750


def table(directory, name):
    for path, opener in ((directory / name, open), (directory / (name + '.gz'), gzip.open)):
        if path.exists():
            with opener(path, 'rt', newline='', errors='replace') as handle:
                yield from csv.DictReader(handle)
            return


def level(game_id):
    mission = int(re.search(r'-m(\d+)/', game_id).group(1))
    return 1 if mission == 1 else min(9, (mission + 4) // 3) if mission < 22 else 9, mission


def parse(line):
    head, _, body = line.partition(' {')
    record = dict(part.split('=', 1) for part in head.split())
    data = json.loads('{' + body) if body else {}
    return {'minute': float(record['minute']), 'house': int(record['house']), 'role': record['role'],
            'difficulty': int(record['difficulty']), 'event': record['event'], 'data': data}


def integer(row, key):
    try:
        return int(float(row.get(key) or 0))
    except ValueError:
        return 0


class Game:
    def __init__(self, row):
        self.row, self.id = row, row['id'].split('#')[0]
        self.events, self.samples = [], collections.defaultdict(dict)   # samples[minute][house]
        self.outcome, self.minutes = row['outcome'], float(row['minutes'] or 0)

    def read(self):
        """The facts the reading below is based on."""
        partner = {m: next((s for s in houses.values() if s['team'] == '1'), None)
                   for m, houses in self.samples.items()}
        enemy_sum = lambda m, key: sum(integer(s, key) for s in self.samples[m].values() if s['team'] != '1')
        facts = {}
        openings = [e for e in self.events if e['event'] == 'campaign_opening']
        facts['trigger'] = min((e['data']['trigger_cycle'] for e in openings), default=0) / CYCLES_PER_MINUTE
        waves = [e for e in self.events if e['event'] == 'ground_hunt' and e['role'] == 'enemy']
        hunts = [e for e in self.events if e['event'] == 'ground_hunt' and e['role'] == 'partner']
        facts['first_wave'] = waves[0]['minute'] if waves else None
        facts['waves'], facts['wave_value'] = len(waves), sum(e['data'].get('value', 0) for e in waves)
        facts['over_cap'] = sum(1 for e in waves
                                if 0 < e['data'].get('alliance_value_cap', -1) < e['data'].get('value', 0))
        facts['first_hunt'] = hunts[0]['minute'] if hunts else None
        facts['hunts'], facts['hunt_value'] = len(hunts), sum(e['data'].get('value', 0) for e in hunts)
        minute = int(facts['first_wave']) if waves else None
        state = partner.get(minute) if minute is not None else None
        facts['partner_army_at_first_wave'] = integer(state, 'army_value') if state else None
        facts['enemy_army_at_first_wave'] = enemy_sum(minute, 'army_value') if state else None
        facts['partner_turrets_at_first_wave'] = integer(state, 'rocket') + integer(state, 'gun') if state else None
        # Income stall: three minutes with under 300 gross while at least three harvesters live.
        facts['income_stall'] = None
        for m in sorted(partner):
            now, later = partner.get(m), partner.get(m + 3)
            if now and later and m >= 6 and integer(now, 'harvesters') >= 3 \
                    and integer(later, 'spice_gross') - integer(now, 'spice_gross') < 300:
                facts['income_stall'] = m
                break
        peak = max(((integer(s, 'army_value'), m) for m, s in partner.items() if s), default=(0, 0))
        facts['partner_peak_army'], facts['partner_peak_minute'] = peak
        last = partner.get(max(partner)) if partner else None
        facts['partner_gross'] = integer(last, 'spice_gross') if last else 0
        facts['partner_lost'], facts['partner_killed'] = (integer(last, 'loss_value'), integer(last, 'kill_value')) if last else (0, 0)
        # The partner's own attack cost it most of its army within four minutes.
        facts['thrown'] = None
        for e in hunts:
            before, after = partner.get(int(e['minute'])), partner.get(int(e['minute']) + 4)
            if before and after and integer(before, 'army_value') >= 2000 \
                    and integer(after, 'army_value') * 5 < integer(before, 'army_value') * 2:
                facts['thrown'] = e['minute']
                break
        return facts

    def reading(self, facts):
        if self.outcome == 'won':
            return 'won'
        if self.outcome != 'lost':
            return self.outcome
        first = facts['first_wave']
        if first is None:
            return 'lost without any opponent wave'
        if self.minutes <= first + 6:
            return 'overrun by the opening waves'
        if facts['thrown'] is not None:
            return f"partner lost its army attacking at {facts['thrown']:.0f} min"
        if facts['income_stall'] is not None and facts['income_stall'] < self.minutes - 5:
            return f"partner income stopped at {facts['income_stall']} min"
        return 'worn down by repeated waves'

    def story(self, out):
        facts = self.read()
        out.write(f"\n=== {self.id}  {self.outcome} at {self.minutes:.1f} min  [{self.reading(facts)}]\n")
        out.write(f"trigger {facts['trigger']:.1f} min; first opponent wave {facts['first_wave']}; "
                  f"partner army then {facts['partner_army_at_first_wave']} with "
                  f"{facts['partner_turrets_at_first_wave']} turrets vs opponents {facts['enemy_army_at_first_wave']}\n")
        deferral = {}
        assaults = collections.Counter()
        for e in self.events:
            d, who = e['data'], f"h{e['house']} {e['role']}"
            if e['event'] == 'campaign_opening':
                out.write(f"  {e['minute']:5.1f} {who} d{e['difficulty']} opens at "
                          f"{d['opening_cycle'] / CYCLES_PER_MINUTE:.1f} (trigger {d['trigger_cycle'] / CYCLES_PER_MINUTE:.1f} "
                          f"+ {d['delay_ms'] / 1000:.0f}s); wave cap {d.get('lever_wave_value')}, "
                          f"army limit {d.get('military_limit')}, harvesters {d.get('harvester_limit')}, "
                          f"repair yards {d.get('repair_yard_target')}\n")
            elif e['event'] == 'ground_hunt':
                out.write(f"  {e['minute']:5.1f} {who} WAVE {d.get('members')}u / {d.get('value')} "
                          f"(cap {d.get('alliance_value_cap')}, out now {d.get('active_value')}, army {d.get('army_value')})\n")
            elif e['event'] == 'attack_deferred':
                key = (d.get('reason'), d.get('required_military', 0) // 1000)
                if deferral.get(e['house']) != key:
                    deferral[e['house']] = key
                    out.write(f"  {e['minute']:5.1f} {who} holds: {d.get('reason')} "
                              f"(has {d.get('military', d.get('available_value'))}, needs {d.get('required_military')})\n")
            elif e['event'] == 'campaign_scripted_assault':
                assaults[(int(e['minute']), e['house'])] += 1
            elif e['event'] == 'campaign_opening_missing_trigger':
                out.write(f"  {e['minute']:5.1f} {who} has no trigger: never attacks\n")
        if assaults:
            out.write('  scripted reinforcements (minute:house x units): '
                      + ' '.join(f"{m}:h{h}x{n}" for (m, h), n in sorted(assaults.items())) + '\n')
        out.write('  min  ' + '  '.join(f"{'h' + h + ('P' if s['team'] == '1' else 'E'):>4} army  hv   cred  str tur ry  lost"
                                        for h, s in sorted(self.samples[min(self.samples)].items())) + '\n')
        for m in sorted(self.samples):
            if m % 3:
                continue
            cells = []
            for h, s in sorted(self.samples[m].items()):
                cells.append(f"{integer(s, 'army_value'):9d} {integer(s, 'harvesters'):3d} {integer(s, 'credits'):6d} "
                             f"{integer(s, 'structures'):4d} {integer(s, 'rocket') + integer(s, 'gun'):3d} "
                             f"{integer(s, 'repair_yards'):2d} {integer(s, 'loss_value'):5d}")
            out.write(f"  {m:3d}  " + '  '.join(cells) + '\n')
        out.write(f"  partner: gross income {facts['partner_gross']}, peak army {facts['partner_peak_army']} at "
                  f"{facts['partner_peak_minute']} min, lost {facts['partner_lost']}, killed {facts['partner_killed']}, "
                  f"attacks {facts['hunts']} / {facts['hunt_value']}; opponents: waves {facts['waves']} / "
                  f"{facts['wave_value']}, over cap {facts['over_cap']}\n")


def load(directories):
    games = {}
    for directory in map(Path, directories):
        local = {}
        for row in table(directory, 'games.csv'):
            local[row['id']] = games[(str(directory), row['id'])] = Game(row)
        for row in table(directory, 'campaign-ai.csv'):
            if row['id'] in local:
                local[row['id']].events.append(parse(row['line']))
        for row in table(directory, 'samples.csv'):
            if row['id'] in local and (integer(row, 'structures') or integer(row, 'army_units')):
                local[row['id']].samples[integer(row, 'minute')][row['house']] = row
    return [game for game in games.values() if game.samples]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('results', nargs='+')
    parser.add_argument('--game', help='Print the full story of games whose ID matches this regex')
    parser.add_argument('--lost', action='store_true', help='With --game: only games that were not won')
    args = parser.parse_args()
    games = load(args.results)
    if args.game:
        for game in games:
            if re.search(args.game, game.id) and not (args.lost and game.outcome == 'won'):
                game.story(sys.stdout)
        return
    cells = collections.defaultdict(lambda: collections.Counter())
    for game in sorted(games, key=lambda g: (g.id.split('/')[1], level(g.id), g.id)):
        facts = game.read()
        reading = game.reading(facts)
        pair, (lvl, mission) = game.id.split('/')[1].replace('partner-', '').replace('-quantbot', ''), level(game.id)
        cells[(pair, lvl)][re.sub(r' at [\d.]+ min', '', reading)] += 1
        print(f"{game.id}: {game.outcome} {game.minutes:.0f}m; trigger {facts['trigger']:.0f}, first wave "
              f"{facts['first_wave']}, partner {facts['partner_army_at_first_wave']} v {facts['enemy_army_at_first_wave']}; "
              f"waves {facts['waves']}/{facts['wave_value']}, partner attacks {facts['hunts']}/{facts['hunt_value']}, "
              f"peak {facts['partner_peak_army']}; {reading}")
    print('\npair, level: won/games; readings of the rest')
    for (pair, lvl), counts in sorted(cells.items()):
        total = sum(counts.values())
        rest = ', '.join(f"{n} {text}" for text, n in counts.most_common() if text != 'won')
        print(f"{pair:16s} L{lvl}: {counts['won']:2d}/{total:2d}  {rest}")


if __name__ == '__main__':
    main()
