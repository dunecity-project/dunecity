#!/usr/bin/env python3
"""Read-only native AI scorecards; no material adjudication or scalar fitness.

``scorecard(run)`` validates the entire selected run before returning long rows.
Input is the run object in the baseline-league manifest. Optional ``ids`` selects
explicit game IDs, ``options_receipt`` supplies the queue's frozen full INI
receipt, and ``diagnostics`` is a CLI-only output (never written by scorecard).
Historical fixtures use games[] plus off_options_by_mod/on_options_by_mod receipts.
Generic fixtures use games[] with id/mod/seed/difficulty/research, roster,
map_sha256, options_sha256 and optional map_file/options_file paths. Optional
genome_phases_sha256 pins are validated separately, never used as pairing keys;
the native games table and optional run diagnostics retain that treatment receipt.

Summary minute is -1. Checkpoints are exact samples, not nearest observations.
Actual samples always take precedence over inferred zeros. Only a team declared
defeated by the native game result has an absorbing post-game zero; timeout
inactivity is not death, and its future checkpoints are censored. isAlive() is
a current inventory predicate: pending deliveries can revive an inactive house.
First inactivity is retained separately from the start of the final inactive
episode. Legacy first-inactive latches never masquerade as terminal timestamps;
unidentified final-inactive FFA placement is unavailable, not material-adjudicated.
First dispatch has an observed indicator; only native-declared defeat competes
with its observation. Inactive timeout houses remain administratively censored.
Army/idle-money means are cycle-weighted left holds over the actual, complete
sample timeline, including observed revivals and any retained inactive inventory.
Economy stall reuses audit.detect's predicate and four-minute persistence over
eligible, currently active base exposure.

Adoption is a native lifetime completed/delivered-building indicator (positive
ownership is also evidence); sparse complete native built ledgers certify zero.
Exact first completion/ownership and sampled first ownership are distinct rows.
Special built totals are the original native built ledgers for items 28/29/40,
not orders, kill credits or reconstructed live counts. Live shares divide their
replacement value by the sampled army replacement value. A known empty army has
an undefined (not_applicable) share, never an invented 0/0. Summary-only core
objectives include city development, supported research and each named special's
built count/share separately; checkpoint rows remain diagnostic. Nonempty-army
minutes, episodes and snapshot counts disclose the conditional share exposure.
Only one house/game/metric/checkpoint row is emitted. Seed blocking belongs to
the gate; snapshots, army episodes and houses are not independent replicates.
"""
import argparse
import base64
import configparser
import csv
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import sys
import tarfile
from collections import Counter, defaultdict

import match_score

SCHEMA_VERSION = 1
COLUMNS = ('schema_version', 'release', 'source_commit', 'baseline_release_source',
           'arm', 'id', 'seed', 'mod', 'difficulty', 'research', 'house', 'team',
           'map_sha256', 'options_sha256', 'context_sha256', 'content_hash',
           'object_data_hash', 'metric', 'minute', 'value', 'status', 'reason')
CHECKPOINTS = (5, 10, 20, 30, 40)
CYCLES_PER_MINUTE = 3750
CORE_FAMILIES = ('win_score', 'kill_value', 'attack_count', 'army_value_time_mean',
                 'adopt_light_factory', 'adopt_heavy_factory', 'adopt_ix',
                 'adopt_starport', 'adopt_palace', 'special_built',
                 'economy_stall_fraction', 'base_loss_fraction',
                 'zone_residential', 'zone_industrial', 'zone_commercial', 'population', 'research_level',
                 'devastator_built', 'deviator_built', 'sonic_tank_built',
                 'devastator_live_value_share', 'deviator_live_value_share', 'sonic_tank_live_value_share')
NATIVE_SPECIAL_HOUSES = (0, 1, 2, 3, 4, 5)
SPECIAL_ITEMS = {'devastator': 28, 'deviator': 29, 'sonic_tank': 40}
TECHNOLOGIES = {'light_factory': ('light', 7), 'heavy_factory': ('heavy', 4),
                'ix': ('ix', 6), 'starport': ('starport', 16), 'palace': ('palace', 8)}
HOUSE_IDS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3,
             'sardaukar': 4, 'mercenary': 5}
STRUCTURE_ITEMS = set(range(1, 27)) | {48, 49, 56, 57, 58, 60, 61, 62, 65,
                                     71, 72, 73, 76, 85, 86, 88}


def _definition(metric, direction='higher', family=None, applicability='all'):
    relative = .10 if metric in (
        'kill_value', 'attack_count', 'army_value_time_mean', 'special_built',
        'zone_residential', 'zone_industrial', 'zone_commercial', 'population',
        'devastator_built', 'deviator_built', 'sonic_tank_built') else 0.0
    absolute = .25 if metric == 'research_level' else .10 if metric.startswith('adopt_') else .05 if (
        metric in ('win_score', 'economy_stall_fraction', 'base_loss_fraction') or
        metric.endswith('_live_value_share')) else 0.0
    return dict(direction=direction, family=family or metric, core=metric in CORE_FAMILIES,
                core_minutes=(-1,), practical_absolute_margin=absolute,
                practical_relative_margin=relative, applicability=applicability)


METRIC_DEFINITIONS = {metric: _definition(metric, 'lower' if metric in (
    'economy_stall_fraction', 'base_loss_fraction') else 'higher') for metric in CORE_FAMILIES}
for _metric in ('win', 'placement', 'first_attack_observed', 'first_attack_minute',
                'alive_followup_minute', 'army_value', 'army_units', 'harvesters', 'refineries',
                'idle_money', 'idle_money_time_mean', 'structures_lost',
                'special_live_value_share', 'army_nonempty_minutes',
                'army_nonempty_episodes', 'army_nonempty_snapshots', 'currently_active',
                'declared_defeated', 'first_inactive_minute', 'terminal_inactive_since_minute',
                'revival_count', 'revival_observed', 'active_minutes', 'inactive_minutes', 'inactive_time_fraction'):
    METRIC_DEFINITIONS[_metric] = _definition(_metric, 'lower' if _metric in (
        'placement', 'first_attack_minute', 'idle_money', 'idle_money_time_mean',
        'structures_lost', 'declared_defeated', 'inactive_minutes', 'inactive_time_fraction') else 'higher')
for _metric in ('zone_residential', 'zone_industrial', 'zone_commercial', 'population'):
    METRIC_DEFINITIONS[_metric] = _definition(_metric, applicability='city')
METRIC_DEFINITIONS['research_level'] = _definition('research_level', applicability='research_on')
for _label in TECHNOLOGIES:
    for _kind in ('first_completed', 'first_owned', 'first_sampled_owned'):
        _metric = _kind + '_' + _label + '_minute'
        METRIC_DEFINITIONS[_metric] = _definition(_metric, 'lower', 'adopt_' + _label)
for _label in SPECIAL_ITEMS:
    for _suffix in ('built', 'live_value_share'):
        _metric = _label + '_' + _suffix
        METRIC_DEFINITIONS[_metric] = _definition(_metric)


class ScorecardError(ValueError):
    """Invalid source/provenance/completion; a run must not partially succeed."""


def _require(condition, message):
    if not condition:
        raise ScorecardError(message)


def _number(value, label='numeric field'):
    if value is None or value == '':
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ScorecardError('Malformed %s: %r' % (label, value))
    _require(math.isfinite(result), 'Nonfinite %s' % label)
    return int(result) if result.is_integer() else result


def _integer(value, label):
    number = _number(value, label)
    _require(number is not None and int(number) == number, 'Missing/noninteger ' + label)
    return int(number)


def _hash(value, label, lengths=(64,)):
    _require(isinstance(value, str) and len(value) in lengths and
             re.fullmatch('[0-9a-f]+', value) is not None, 'Missing/invalid ' + label)
    return value


def _sha(contents):
    return hashlib.sha256(contents).hexdigest()


def context_sha256(game_id, mod, difficulty, research, seed):
    return _sha(json.dumps(dict(id=game_id, mod=mod, difficulty=difficulty,
                                research=research, seed=int(seed)),
                           sort_keys=True, separators=(',', ':')).encode('utf-8'))


def _json_file(path):
    try:
        with Path(path).open(encoding='utf-8') as handle:
            return json.load(handle)
    except (OSError, ValueError) as error:
        raise ScorecardError('Cannot read JSON %s: %s' % (path, error))


def _csv_path(path):
    path = Path(path)
    return path if path.is_file() else Path(str(path) + '.gz')


def _csv_rows(path, required=False):
    path = _csv_path(path)
    if not path.is_file():
        _require(not required, 'Missing evidence: ' + str(path))
        return
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(str(path), 'rt', encoding='utf-8', newline='') as handle:
        reader = csv.DictReader(handle)
        _require(reader.fieldnames and len(reader.fieldnames) == len(set(reader.fieldnames)),
                 'Missing/duplicate CSV header: ' + str(path))
        for row in reader:
            _require(None not in row and all(value is not None for value in row.values()),
                     'Malformed CSV row: ' + str(path))
            yield row


def _path(base, value):
    path = Path(value)
    return path if path.is_absolute() else base / path


def _compact_bytes(path):
    path = Path(path)
    try:
        opener = gzip.open if path.suffix == '.gz' else open
        with opener(str(path), 'rb') as handle:
            contents = handle.read(8 * 1024 * 1024 + 1)
        _require(len(contents) <= 8 * 1024 * 1024, 'Oversize compact evidence: ' + str(path))
        return contents
    except OSError as error:
        raise ScorecardError('Cannot read compact evidence %s: %s' % (path, error))


def _receipt_bytes(path):
    path = Path(path)
    if path.suffix == '.json':
        receipt = _json_file(path)
        try:
            contents = base64.b64decode(receipt['content_b64'], validate=True)
        except (KeyError, ValueError) as error:
            raise ScorecardError('Invalid frozen options receipt %s: %s' % (path, error))
        _require(_sha(contents) == receipt.get('sha256'), 'Frozen options receipt hash mismatch: ' + str(path))
        return contents
    return _compact_bytes(path)


def _options_settings(contents):
    config = configparser.ConfigParser(interpolation=None)
    try:
        config.read_string(contents.decode('utf-8'))
        _require(config.has_section('Game Options'), 'Full options missing [Game Options]')
        return {key: config.getboolean('Game Options', name) for key, name in (
            ('city_effects', 'City Effects'), ('research', 'Research Lab'))
                if config.has_option('Game Options', name)}
    except (ValueError, UnicodeError, configparser.Error) as error:
        raise ScorecardError('Malformed full game options: ' + str(error))


def _replay_map(replay):
    """Replay player string, two int8s, filename string, exact filedata string.

    GameInitSettings(InputStream&) is the native serialization contract. Do not
    re-generate a map from its seed or hash the rendered engine map instead.
    """
    position = 0

    def string():
        nonlocal position
        _require(position + 4 <= len(replay), 'Truncated replay string size')
        size = struct.unpack_from('<I', replay, position)[0]
        position += 4
        _require(size <= 8 * 1024 * 1024 and position + size <= len(replay), 'Truncated/oversize replay string')
        value = replay[position:position + size]
        position += size
        return value

    string()
    _require(position + 2 <= len(replay), 'Truncated replay game header')
    game_type = replay[position]
    position += 2
    _require(game_type in (2, 3), 'Replay is not an exact custom-map game')
    string()
    contents = string()
    _require(bool(contents), 'Replay contains no raw map filedata')
    return contents


def _roster(sides):
    result = {}
    for side in sides.split(',') if sides else ():
        pieces = side.split(':')
        _require(len(pieces) == 3, 'Malformed native AI roster')
        name, team, controller = pieces
        house = HOUSE_IDS.get(name.lower())
        if house is None:
            house = _integer(name, 'roster house')
        _require(house not in result, 'Duplicate native roster house')
        _require(controller not in ('human', 'HumanPlayer', 'observer', ''), 'Non-native-AI roster entrant')
        result[house] = _integer(team, 'roster team')
    return result


class _Evidence:
    def __init__(self, run):
        self.run = dict(run)
        for field in ('release', 'source_commit', 'arm', 'research', 'mod', 'results', 'format'):
            _require(run.get(field) is not None and run.get(field) != '', 'Missing run field: ' + field)
        _require(run['format'] in ('historical', 'audit'), 'Unknown evidence format')
        _require(run['research'] in ('off', 'on'), 'Research treatment must be explicit off/on')
        _hash(run['source_commit'], 'run source_commit', (40,))
        self.release_source = run.get('baseline_release_source') or run['source_commit']
        _hash(self.release_source, 'baseline_release_source', (40,))
        self.root = Path(run['results'])
        self.fixture_base = self.root
        self.fixture = {}
        if run.get('fixtures'):
            if isinstance(run['fixtures'], dict):
                self.fixture = run['fixtures']
            else:
                self.fixture_base = Path(run['fixtures']).parent
                self.fixture = _json_file(run['fixtures'])
        _require(run['format'] != 'historical' or self.fixture, 'Historical evidence requires exact fixture manifest')
        self.games = {}
        requested = set(run['ids']) if 'ids' in run else None
        _require(requested is None or bool(requested), 'Empty game selection')
        for game in _csv_rows(self.root / 'games.csv', required=True):
            game_id = game.get('id')
            _require(game_id, 'Game has no original context id')
            if requested is not None and game_id not in requested:
                continue
            _require(game_id not in self.games, 'Duplicate game completion: ' + game_id)
            self.games[game_id] = game
        _require(self.games, 'No selected games')
        _require(requested is None or requested == set(self.games), 'Selected game IDs missing from completion table')
        self.houses = defaultdict(dict)
        self.samples = defaultdict(dict)
        self.tech = {}
        self.tech_samples = defaultdict(dict)
        self.units = defaultdict(dict)
        self.composition = defaultdict(dict)
        self.waves = defaultdict(list)
        self.raw_cache = {}
        self.archives = None
        self.provenance = {}
        self._tables()
        for game_id in sorted(self.games):
            self._validate_game(game_id)

    def _identity(self, row):
        game_id = row.get('id')
        _require(game_id, 'Evidence row missing context id')
        if game_id not in self.games:
            return None
        game = self.games[game_id]
        if row.get('seed', '') != '':
            _require(_integer(row['seed'], 'evidence seed') == _integer(game.get('seed'), 'game seed'),
                     'Evidence seed mismatch: ' + game_id)
        if row.get('mod', ''):
            _require(row['mod'] == self.run['mod'], 'Evidence mod mismatch: ' + game_id)
        return game_id, _integer(row.get('house'), 'evidence house')

    def _tables(self):
        for row in _csv_rows(self.root / 'houses.csv', required=True):
            key = self._identity(row)
            if key is None:
                continue
            game_id, house = key
            _require(house not in self.houses[game_id], 'Duplicate final house: ' + str(key))
            self.houses[game_id][house] = row
        for filename, destination in (('samples.csv', self.samples), ('tech-samples.csv', self.tech_samples)):
            for row in _csv_rows(self.root / filename, required=filename == 'samples.csv'):
                key = self._identity(row)
                if key is None:
                    continue
                cycle = _integer(row.get('cycle'), filename + ' cycle')
                _require(cycle not in destination[key], 'Duplicate snapshot: %s %s %s' % (filename, key, cycle))
                destination[key][cycle] = row
        for row in _csv_rows(self.root / 'tech-houses.csv'):
            key = self._identity(row)
            if key is not None:
                _require(key not in self.tech, 'Duplicate technology house: ' + str(key))
                self.tech[key] = row
        for filename, destination, field in (('units.csv', self.units, 'item'),
                                              ('composition.csv', self.composition, 'slot')):
            seen = set()
            for row in _csv_rows(self.root / filename):
                key = self._identity(row)
                if key is None:
                    continue
                cycle = _integer(row.get('cycle'), filename + ' cycle')
                identity = key, cycle, row.get(field)
                _require(identity not in seen, 'Duplicate native item/slot: ' + str(identity))
                seen.add(identity)
                item = _integer(row.get('item'), filename + ' item')
                if item in SPECIAL_ITEMS.values():
                    point = destination.setdefault((key, cycle), {})
                    _require(item not in point, 'Duplicate special item at snapshot: ' + str(identity))
                    point[item] = row
        seen = set()
        for row in _csv_rows(self.root / 'waves.csv'):
            key = self._identity(row)
            if key is not None:
                cycle = _integer(row.get('cycle'), 'ground_hunt cycle')
                _require(0 <= cycle, 'Negative attack cycle')
                members = _integer(row.get('members'), 'ground_hunt members')
                _require(members >= 0, 'Negative ground_hunt membership')
                identity = key, cycle
                _require(identity not in seen, 'Duplicate ground_hunt callback: ' + str(identity))
                seen.add(identity)
                if members:
                    self.waves[key].append(cycle)

    def fixture_for(self, game):
        candidates = [entry for entry in self.fixture.get('games', ())
                      if entry.get('id') == game['id'] and entry.get('mod', self.run['mod']) == self.run['mod']
                      and entry.get('research', self.run['research']) == self.run['research']]
        _require(len(candidates) == 1 or not self.fixture,
                 'Missing/ambiguous exact fixture: ' + game['id'])
        return candidates[0] if candidates else {}

    def _archive_index(self):
        if self.archives is not None:
            return
        self.archives = {}
        directory = self.root / 'cluster-evidence'
        tables = sorted(directory.glob('*/games.csv')) + sorted(directory.glob('*/games.csv.gz'))
        for table in tables:
            if table.suffix == '.gz' and table.with_suffix('').is_file():
                continue
            archive = table.parent / 'probe-evidence.tar.gz'
            if not archive.is_file():
                continue
            for row in _csv_rows(table):
                if row.get('id') in self.games:
                    previous = self.archives.get(row['id'])
                    _require(previous is None or previous == archive, 'Ambiguous native archives: ' + row['id'])
                    self.archives[row['id']] = archive

    def raw(self, game_id):
        if game_id in self.raw_cache:
            return self.raw_cache[game_id]
        self._archive_index()
        game = self.games[game_id]
        entry = self.fixture_for(game)
        archive = self.archives.get(game_id)
        if entry.get('native_archive'):
            archive = _path(self.fixture_base, entry['native_archive'])
        if archive is None and (self.root / 'probe-evidence.tar.gz').is_file():
            archive = self.root / 'probe-evidence.tar.gz'
        bundle = {}
        for field, name in (('summary_file', 'summary.json'), ('setup_file', 'setup.json'),
                            ('replay_file', 'auto.rpl')):
            if entry.get(field):
                contents = _compact_bytes(_path(self.fixture_base, entry[field]))
                bundle[name] = json.loads(contents) if name.endswith('.json') else contents
        if not bundle and archive is not None:
            # Stream past giant events.jsonl members; never decode, materialize or
            # extract them. Compact native summaries, not callbacks, give ledgers.
            try:
                with tarfile.open(str(archive), 'r|gz') as handle:
                    for member in handle:
                        suffix = Path(member.name).name
                        if suffix not in ('summary.json', 'setup.json', 'auto.rpl') or not member.isfile():
                            continue
                        _require(member.size <= 8 * 1024 * 1024, 'Oversize compact native evidence: ' + member.name)
                        contents = handle.extractfile(member).read()
                        _require(suffix not in bundle, 'Ambiguous compact native evidence in ' + str(archive))
                        bundle[suffix] = json.loads(contents) if suffix.endswith('.json') else contents
                        bundle['prefix'] = member.name.split('/profile/', 1)[0] if '/profile/' in member.name else str(Path(member.name).parent)
            except (OSError, ValueError, tarfile.TarError) as error:
                raise ScorecardError('Cannot read native archive %s: %s' % (archive, error))
        elif not bundle:
            diagnostic = game.get('diagnostics')
            if diagnostic:
                directory = _path(self.root, diagnostic)
                for name in ('summary.json', 'setup.json'):
                    if (directory / name).is_file():
                        bundle[name] = _json_file(directory / name)
        if bundle:
            setup = bundle.get('setup.json', {})
            summary = bundle.get('summary.json', {})
            native_setup = summary.get('setup', setup)
            if native_setup.get('game', {}).get('id'):
                # A root archive may only retain one of the collected games.
                if native_setup['game']['id'] != game_id:
                    bundle = {}
            elif bundle.get('prefix'):
                expected = 'g' + _sha(game_id.encode())[:12]
                if Path(bundle['prefix']).name != expected:
                    bundle = {}
        self.raw_cache[game_id] = bundle
        return bundle

    def _native_houses(self, game_id):
        bundle = self.raw(game_id)
        summary = bundle.get('summary.json', {})
        finals = summary.get('final', summary.get('game_summary', []))
        if not finals:
            return None
        _require(len(finals) == 1 and finals[0].get('event') == 'game_summary', 'Invalid native final summary: ' + game_id)
        final = finals[0]
        _require(_integer(final.get('cycle'), 'native final cycle') == self.provenance[game_id]['end'],
                 'Native summary completion cycle mismatch: ' + game_id)
        raw_houses = final.get('data', {}).get('houses', {})
        _require({int(house) for house in raw_houses} == set(self.houses[game_id]), 'Native final roster mismatch: ' + game_id)
        pin = summary.get('sourceCommit') or summary.get('setup', {}).get('source_commit')
        _require(pin == self.run['source_commit'], 'Native summary source pin mismatch: ' + game_id)
        _require(not summary.get('workingTreeModified') and not summary.get('sourceFingerprintError'),
                 'Modified/uncertified native summary source: ' + game_id)
        for house, row in self.houses[game_id].items():
            observed = raw_houses[str(house)]
            _require(_integer(observed.get('team'), 'native team') == int(row['team']) and
                     _integer(observed.get('alive'), 'native alive') == int(row['alive']),
                     'Native summary house outcome mismatch: ' + game_id)
        return raw_houses

    def _validate_game(self, game_id):
        game = self.games[game_id]
        run = self.run
        _require(not game.get('error'), 'Incomplete/error game %s: %s' % (game_id, game.get('error')))
        _require(game.get('source_commit') == run['source_commit'], 'Source pin mismatch: ' + game_id)
        _require(not game.get('source_fingerprint_error'), 'Source fingerprint error: ' + game_id)
        if run['format'] == 'audit':
            _hash(game.get('source_fingerprint'), 'audit source_fingerprint')
        if game.get('baseline_release_source'):
            _require(game['baseline_release_source'] == self.release_source, 'Baseline source mismatch: ' + game_id)
        if game.get('mod'):
            _require(game['mod'] == run['mod'], 'Game mod mismatch: ' + game_id)
        if game.get('research'):
            _require(game['research'] == run['research'], 'Game research treatment mismatch: ' + game_id)
        if run['format'] == 'historical' and run['research'] == 'on':
            _require(game.get('research') == 'on', 'Historical research ON is unsupported/unrecorded')
        seed = _integer(game.get('seed'), 'game seed')
        roster = self.houses[game_id]
        _require(bool(roster), 'Missing final houses: ' + game_id)
        fixture = self.fixture_for(game)
        context_id = game.get('context_id') or game_id
        if game.get('context_id') or fixture.get('context_id'):
            _require(bool(game.get('context_id')) and game['context_id'] == fixture.get('context_id'),
                     'Explicit context ID receipt mismatch: ' + game_id)
        expected = {int(house): int(team) for house, team in fixture.get('roster', [])} or _roster(game.get('sides', ''))
        _require(expected and len(set(expected.values())) >= 2, 'Missing native opponent roster: ' + game_id)
        _require(set(roster) == set(expected), 'Incomplete/extra final-house roster: ' + game_id)
        for source in (self.samples, self.tech_samples, self.tech, self.waves):
            _require(all(key[1] in roster for key in source if key[0] == game_id),
                     'Evidence contains an unregistered house: ' + game_id)
        for source in (self.units, self.composition):
            _require(all(key[0][1] in roster for key in source if key[0][0] == game_id),
                     'Item evidence contains an unregistered house: ' + game_id)
        if fixture:
            _require(_integer(fixture.get('seed'), 'fixture seed') == seed, 'Fixture seed mismatch: ' + game_id)
            _require(fixture.get('difficulty', game.get('difficulty')) == game.get('difficulty'), 'Fixture difficulty mismatch: ' + game_id)
        points = [cycle for house in roster for cycle in self.samples[(game_id, house)]]
        _require(bool(points), 'Missing native snapshots: ' + game_id)
        end = _integer(game.get('cycle'), 'final cycle') if game.get('cycle', '') != '' else max(points)
        _require(end > 0 and max(points) == end, 'Missing final-cycle snapshot: ' + game_id)
        if game.get('minutes', '') != '':
            _require(abs(float(game['minutes']) - end / CYCLES_PER_MINUTE) <= .011,
                     'Game minutes/final cycle mismatch: ' + game_id)
        survivors = set()
        for house, final in roster.items():
            _require(_integer(final.get('team'), 'house team') == expected[house], 'Native team mismatch: ' + game_id)
            alive = _integer(final.get('alive'), 'house alive')
            _require(alive in (0, 1), 'Invalid house alive: ' + game_id)
            if alive:
                survivors.add(expected[house])
            self._validate_lifecycle(final, end)
            samples = self.samples[(game_id, house)]
            _require(samples and min(samples) == 0 and max(samples) == end, 'Incomplete house snapshot endpoints: ' + game_id)
            for cycle, point in samples.items():
                _require(0 <= cycle <= end and _integer(point.get('team'), 'sample team') == expected[house],
                         'Invalid snapshot cycle/team: ' + game_id)
                _require(_integer(point.get('alive'), 'sample alive') in (0, 1), 'Invalid sample alive flag')
                self._validate_lifecycle(point, cycle)
            _require(int(samples[end]['alive']) == alive, 'Final sample/house alive mismatch: ' + game_id)
            for field in ('first_inactive_cycle', 'terminal_inactive_since_cycle', 'revival_count', 'inactive_cycles'):
                if final.get(field, '') != '' and samples[end].get(field, '') != '':
                    _require(_number(final[field], field) == _number(samples[end][field], field),
                             'Final house/sample lifecycle mismatch: ' + game_id)
        winner = _integer(game.get('winner_team'), 'winner_team')
        outcome = game.get('outcome')
        _require(outcome in ('won', 'lost', 'decided', 'extinct', 'time_limit'), 'Unrecognized/unfinished native outcome: ' + game_id)
        if winner == 0:
            _require(outcome in ('time_limit', 'extinct') and len(survivors) != 1 and
                     (outcome != 'extinct' or not survivors), 'False no-winner native outcome: ' + game_id)
        else:
            _require(outcome not in ('time_limit', 'extinct') and survivors == {winner}, 'Winner/roster contradiction: ' + game_id)
        receipt = dict(end=end, seed=seed, fixture=fixture, context_id=context_id)
        self.provenance[game_id] = receipt
        for field in ('content_hash', 'object_data_hash'):
            receipt[field] = _hash(game.get(field), field, (16, 64))
        _hash(game.get('probe_sha256'), 'probe_sha256')
        phase_hash = game.get('genome_phases_sha256', '')
        receipt['genome_phases_sha256'] = _hash(phase_hash, 'genome_phases_sha256') if phase_hash else ''
        for pin in (run.get('genome_phases_sha256'), fixture.get('genome_phases_sha256')):
            if pin:
                _require(receipt['genome_phases_sha256'] == _hash(pin, 'expected genome_phases_sha256'),
                         'Genome phase schedule pin mismatch: ' + game_id)
        options = self.fixture.get(run['research'] + '_options_by_mod', {}).get(run['mod'], {})
        expected_options = fixture.get('options_sha256') or options.get('sha256')
        if fixture:
            _hash(expected_options, 'fixture options_sha256')
        if fixture.get('options_sha256') and options.get('sha256'):
            _require(fixture['options_sha256'] == options['sha256'],
                     'Conflicting fixture options receipts: ' + game_id)
        option_path = fixture.get('options_file') or run.get('options_file')
        option_receipt = run.get('options_receipt') or options.get('original_frozen_input_receipt')
        option_bytes = None
        if option_path:
            option_bytes = _receipt_bytes(_path(self.fixture_base, option_path))
        elif option_receipt and Path(option_receipt).is_file():
            option_bytes = _receipt_bytes(option_receipt)
        elif options.get('file') and _path(self.fixture_base, options['file']).is_file():
            option_bytes = _receipt_bytes(_path(self.fixture_base, options['file']))
        observed_options = game.get('options_sha256')
        observed_map = game.get('map_sha256')
        # Unchanged audit tables only carry engine FNV hashes. Recover exact raw
        # map bytes and full requested INI receipts, never relabel FNV as SHA256.
        if not observed_map or not observed_options:
            bundle = self.raw(game_id)
            if not observed_map:
                _require(bundle.get('auto.rpl'), 'Missing exact archived map receipt: ' + game_id)
                observed_map = _sha(_replay_map(bundle['auto.rpl']))
            if not observed_options:
                setup = bundle.get('setup.json', {})
                remote = setup.get('game_options', '').replace('\\', '/')
                unit = remote.split('/')[-2] if '/' in remote else ''
                matched = re.fullmatch(r'(.+)\.\d+\.[0-9a-f]+', unit)
                _require(matched is not None, 'Missing full original options input provenance: ' + game_id)
                frozen = Path.home() / '.local/state/dunecity-queue/inputs' / (matched.group(1) + '.json')
                if run.get('options_receipt'):
                    frozen = Path(run['options_receipt'])
                    _require(frozen.stem == matched.group(1), 'Options receipt belongs to another submission')
                archived_options = _receipt_bytes(frozen)
                _require(option_bytes is None or _sha(option_bytes) == _sha(archived_options), 'Original requested options differ: ' + game_id)
                option_bytes = archived_options
                observed_options = _sha(archived_options)
        receipt['map_sha256'] = _hash(observed_map, 'map_sha256')
        receipt['options_sha256'] = _hash(observed_options, 'options_sha256')
        if fixture:
            _require(receipt['map_sha256'] == fixture.get('map_sha256'), 'Exact fixture map mismatch: ' + game_id)
            _require(receipt['options_sha256'] == expected_options, 'Full fixture options mismatch: ' + game_id)
        if option_bytes is not None:
            _require(_sha(option_bytes) == receipt['options_sha256'], 'Full requested options hash mismatch: ' + game_id)
            settings = _options_settings(option_bytes)
            if 'research' in settings:
                _require(settings['research'] == (run['research'] == 'on'), 'Requested research treatment mismatch: ' + game_id)
        else:
            settings = {}
        map_file = fixture.get('map_file')
        if not map_file and fixture.get('map') and (self.fixture_base / 'maps' / fixture['map']).is_file():
            map_file = str(self.fixture_base / 'maps' / fixture['map'])
        if map_file:
            _require(_sha(_compact_bytes(_path(self.fixture_base, map_file))) == receipt['map_sha256'],
                     'Exact map file receipt mismatch: ' + game_id)
        for native_field in ('map_hash', 'options_hash'):
            if fixture.get(native_field):
                _require(game.get(native_field) == fixture[native_field], 'Native options/map provenance mismatch: ' + game_id)
        city = fixture.get('city_effects', options.get('city_effects', settings.get('city_effects')))
        if game.get('city_sim', '') != '':
            native_city = bool(_integer(game['city_sim'], 'city_sim'))
            _require(city is None or bool(city) == native_city, 'City applicability mismatch: ' + game_id)
            city = native_city
        if city is None:
            values = {_integer(row['city_sim'], 'native city_sim') for key, points in self.tech_samples.items()
                      if key[0] == game_id for row in points.values() if row.get('city_sim', '') != ''}
            _require(len(values) <= 1, 'Conflicting city simulation settings: ' + game_id)
            city = bool(next(iter(values))) if values else None
        receipt['city'] = city
        research_values = {_integer(row['research_enabled'], 'research_enabled')
                           for source in (self.samples, self.tech_samples)
                           for key, points in source.items() if key[0] == game_id
                           for row in points.values() if row.get('research_enabled', '') != ''}
        _require(len(research_values) <= 1 and (not research_values or research_values == {int(run['research'] == 'on')}),
                 'Native research treatment mismatch: ' + game_id)
        _require(run['research'] != 'on' or research_values == {1}, 'Research ON unsupported/unobserved: ' + game_id)
        for key, waves in self.waves.items():
            if key[0] == game_id:
                _require(key[1] in roster and all(cycle <= end for cycle in waves), 'Attack outside completed native game')
        # Native archives can contain enormous compressed callback streams.
        # Open them only for exact input receipts or absent compact ledgers;
        # the extended observer already supplies everything without raw I/O.
        need_native = game_id in self.raw_cache
        for number, house in roster.items():
            final = self.samples[(game_id, number)][end]
            fields = ['harvesters', 'refineries', 'built', 'structures_lost']
            if city is not False:
                fields.extend(('zone_residential', 'zone_industrial', 'zone_commercial'))
            need_native |= any(final.get(field, '') == '' for field in fields)
            need_native |= any(house.get(field + '_built', final.get(field + '_built', '')) == ''
                               for field, _ in TECHNOLOGIES.values())
            items = self.units.get(((game_id, number), end), {})
            need_native |= any(items.get(item, {}).get('built', '') == '' for item in SPECIAL_ITEMS.values())
        receipt['native_houses'] = self._native_houses(game_id) if need_native else None

    @staticmethod
    def first_inactive_cycle(house):
        for field in ('first_inactive_cycle', 'eliminated_cycle'):
            if house.get(field, '') != '':
                value = _number(house[field], field)
                return value if value >= 0 else None
        value = _number(house.get('eliminated_s'), 'legacy first-inactive seconds')
        return value * CYCLES_PER_MINUTE / 60 if value is not None and value >= 0 else None

    @staticmethod
    def terminal_cycle(house):
        # Old eliminated_* fields latch FIRST isAlive()==false. They cannot
        # establish the terminal episode, even when the final state is inactive.
        value = _number(house.get('terminal_inactive_since_cycle'), 'terminal_inactive_since_cycle')
        return value if value is not None and value >= 0 and int(house['alive']) == 0 else None

    @classmethod
    def _validate_lifecycle(cls, house, cycle):
        if house.get('first_inactive_cycle', '') != '':
            first_cycle = _integer(house['first_inactive_cycle'], 'first_inactive_cycle')
            _require(-1 <= first_cycle <= cycle, 'First inactivity outside observation horizon')
        first = cls.first_inactive_cycle(house)
        _require(first is None or 0 <= first <= cycle + 4, 'First inactivity outside observation horizon')
        explicitly_never_inactive = house.get('first_inactive_cycle', '') != '' and first is None
        if house.get('terminal_inactive_since_cycle', '') != '':
            terminal = _integer(house['terminal_inactive_since_cycle'], 'terminal_inactive_since_cycle')
            active = _integer(house.get('alive'), 'lifecycle alive')
            _require((active == 1 and terminal == -1) or
                     (active == 0 and 0 <= terminal <= cycle),
                     'Current activity/terminal-inactive episode mismatch')
            _require(first is None or terminal < 0 or terminal >= first,
                     'Terminal inactive episode precedes first inactivity')
            _require(not explicitly_never_inactive or terminal < 0,
                     'Terminal inactivity contradicts explicit no-inactivity record')
        if house.get('revival_count', '') != '':
            revivals = _integer(house['revival_count'], 'revival_count')
            _require(revivals >= 0, 'Negative revival count')
            _require(not explicitly_never_inactive or revivals == 0,
                     'Revival count contradicts explicit no-inactivity record')
            _require(not (int(house['alive']) == 1 and first is not None and revivals == 0),
                     'Active house after first inactivity requires a revival')
        if house.get('inactive_cycles', '') != '':
            inactive = _integer(house['inactive_cycles'], 'inactive_cycles')
            _require(0 <= inactive <= cycle, 'Inactive exposure exceeds observed cycles')
            _require(not explicitly_never_inactive or inactive == 0,
                     'Inactive exposure contradicts explicit no-inactivity record')


def _value(point, field):
    return _number(point.get(field), field) if point else None


def _mean(points, end, getter):
    ordered = sorted(points)
    if not ordered or ordered[0] != 0 or ordered[-1] != end:
        return None
    integral = 0.0
    for start, stop in zip(ordered, ordered[1:]):
        if stop - start > CYCLES_PER_MINUTE:
            return None
        value = getter(points[start])
        if value is None:
            return None
        integral += value * (stop - start)
    return integral / end


def _idle_money(point):
    fields = [_value(point, field) for field in ('credits', 'queue', 'upgrading')]
    if any(value is None for value in fields):
        return None
    return fields[0] if fields[1] == 0 and fields[2] == 0 else 0


def _stall_fraction(points):
    # Exact economy_stall predicate/persistence/eligibility from audit.detect;
    # exposure integration is additional, not the old ever-stalled house flag.
    previous, since = None, None
    flags = {}
    for cycle, point in sorted(points.items()):
        fields = ('alive', 'yards', 'credits', 'built', 'population')
        values = {field: _value(point, field) for field in fields}
        if any(value is None for value in values.values()):
            return None, 'missing_native_stall_fields'
        old = previous or values
        city = bool(values['alive'] and values['yards'] > 0)
        growth = values['built'] > old['built'] or values['population'] > old['population']
        condition = city and values['credits'] < 1000 and not growth
        since = (cycle if since is None else since) if condition else None
        flags[cycle] = (city and cycle >= 4 * CYCLES_PER_MINUTE,
                        since is not None and cycle - since >= 4 * CYCLES_PER_MINUTE)
        previous = values
    exposed = stalled = 0.0
    ordered = sorted(points)
    for start, stop in zip(ordered, ordered[1:]):
        if stop - start > CYCLES_PER_MINUTE:
            return None, 'missing_scheduled_stall_samples'
        if stop > start and flags[start][0]:
            exposed += stop - start
            if flags[start][1]:
                stalled += stop - start
    return (stalled / exposed, 'sampled_persistent_stall_over_eligible_base_time') if exposed else (
        None, 'no_eligible_base_exposure')


def _house_rows(evidence, game_id, number, placings, placement_known):
    game, run = evidence.games[game_id], evidence.run
    house = evidence.houses[game_id][number]
    receipt = evidence.provenance[game_id]
    end = receipt['end']
    points = evidence.samples[(game_id, number)]
    final, initial = points[end], points[0]
    terminal = evidence.terminal_cycle(house)
    active = int(house['alive']) == 1
    defeated = int(game['winner_team']) != 0 and int(house['team']) != int(game['winner_team'])
    native = (receipt['native_houses'] or {}).get(str(number))
    tech = evidence.tech.get((game_id, number), {})
    difficulty = house.get('difficulty') or game['difficulty']
    base = dict(schema_version=SCHEMA_VERSION, release=run['release'], source_commit=run['source_commit'],
                baseline_release_source=evidence.release_source, arm=run['arm'], id=receipt['context_id'],
                seed=receipt['seed'], mod=run['mod'], difficulty=difficulty, research=run['research'],
                house=number, team=int(house['team']), map_sha256=receipt['map_sha256'],
                options_sha256=receipt['options_sha256'],
                context_sha256=context_sha256(receipt['context_id'], run['mod'], difficulty, run['research'], receipt['seed']),
                content_hash=receipt['content_hash'], object_data_hash=receipt['object_data_hash'])
    unsupported = game.get('unsupported_options', '')
    if unsupported:
        try:
            unsupported = json.dumps(json.loads(unsupported), sort_keys=True, separators=(',', ':'))
        except ValueError:
            raise ScorecardError('Malformed unsupported-options disclosure: ' + game_id)
    rows = []

    def emit(metric, value=None, status=None, reason='', minute=-1):
        if status is None:
            status = 'measured' if value is not None else 'unavailable'
        if status == 'measured':
            _require(value is not None and math.isfinite(float(value)), 'Missing measured value: ' + metric)
        if unsupported and unsupported != '{}':
            reason = (reason + ';' if reason else '') + 'unsupported_requested_options=' + unsupported
        row = dict(base, metric=metric, minute=minute, value='' if value is None else value,
                   status=status, reason=reason or ('native_observation' if value is not None else 'missing_native_field'))
        rows.append({field: row[field] for field in COLUMNS})

    def event_time(metric, minute, available, reason):
        if minute is not None:
            _require(0 <= minute <= end / CYCLES_PER_MINUTE + .001, 'First event outside game: ' + metric)
            emit(metric, minute, reason=reason)
        elif not available:
            emit(metric, reason='missing_exact_event_telemetry')
        elif defeated:
            emit(metric, status='not_applicable', reason='competing_event_native_declared_defeat_without_event')
        else:
            emit(metric, end / CYCLES_PER_MINUTE, status='censored', reason='administrative_game_end_without_event')

    placing = placings[number]
    emit('win_score', placing.score if placement_known else None, reason=(
        'match_score_final_team_standing_rank_identified' if placement_known else 'terminal_inactivity_order_unrecorded'))
    emit('placement', placing.place if placement_known else None, reason=(
        'native_final_team_standing_rank_identified' if placement_known else 'terminal_inactivity_order_unrecorded'))
    emit('win', int(int(game['winner_team']) != 0 and int(house['team']) == int(game['winner_team'])),
         reason='native_winner_team_zero_preserves_no_winner')
    emit('kill_value', _value(house, 'kill_value'), reason='native_kill_value_not_enemy_loss_ledger')
    emit('currently_active', int(active), reason='native_current_inventory_predicate_not_permanent_lifecycle')
    emit('declared_defeated', int(defeated), reason='native_declared_winner_excludes_this_team')
    emit('alive_followup_minute', end / CYCLES_PER_MINUTE,
         reason='native_declared_defeat_observation_end' if defeated else 'administrative_game_end_inactivity_not_absorbing')
    first_inactive = evidence.first_inactive_cycle(house)
    first_recorded = any(house.get(field, '') != '' for field in (
        'first_inactive_cycle', 'eliminated_cycle', 'eliminated_s'))
    if first_inactive is not None:
        emit('first_inactive_minute', first_inactive / CYCLES_PER_MINUTE,
             reason='native_first_inactivity_not_permanent_defeat')
    elif first_recorded:
        emit('first_inactive_minute', end / CYCLES_PER_MINUTE, status='censored',
             reason='no_inactivity_observed_before_administrative_end')
    else:
        emit('first_inactive_minute', reason='first_inactivity_unrecorded')
    emit('terminal_inactive_since_minute', terminal / CYCLES_PER_MINUTE if terminal is not None else None,
         status='not_applicable' if active else None,
         reason='currently_active_no_terminal_inactive_episode' if active else (
             'exact_final_inactive_episode_start' if terminal is not None else 'terminal_inactivity_timing_unrecorded'))
    revivals = _value(house, 'revival_count')
    emit('revival_count', revivals, reason='exact_native_inactive_to_active_transitions' if revivals is not None
         else 'legacy_first_inactivity_does_not_count_revivals')
    states = [int(point['alive']) for _, point in sorted(points.items())]
    observed_revival = any(not before and after for before, after in zip(states, states[1:])) or (
        active and first_inactive is not None)
    revival_indicator = int(revivals > 0) if revivals is not None else 1 if observed_revival else (
        0 if first_recorded and first_inactive is None else None)
    emit('revival_observed', revival_indicator, reason='native_revival_count_or_positive_observed_revival_proof')
    inactive = _value(house, 'inactive_cycles')
    emit('inactive_minutes', inactive / CYCLES_PER_MINUTE if inactive is not None else None,
         reason='exact_native_currently_inactive_cycle_exposure')
    emit('active_minutes', (end - inactive) / CYCLES_PER_MINUTE if inactive is not None else None,
         reason='exact_currently_active_cycle_exposure_not_a_permanent_survival_time')
    emit('inactive_time_fraction', inactive / end if inactive is not None else None,
         reason='exact_inactive_cycles_over_actual_game_duration')
    complete = game.get('telemetry_status') == 'complete' if run['format'] == 'historical' else (
        game.get('attack_telemetry') == '1' and game.get('air_kill_telemetry') == 'complete')
    callbacks = _csv_path(evidence.root / 'waves.csv').is_file()
    attacks = sorted(evidence.waves[(game_id, number)])
    recorded = _value(house, 'attack_count')
    recorded_first = _value(house, 'first_attack_minute')
    certified = complete and (callbacks or (run['format'] == 'audit' and recorded is not None))
    count, first = None, None
    if certified:
        count = len(attacks) if callbacks else recorded
        _require(count >= 0 and int(count) == count, 'Invalid native attack count: ' + game_id)
        _require(recorded is None or recorded == count, 'Attack callback/count mismatch: ' + game_id)
        first = attacks[0] / CYCLES_PER_MINUTE if attacks else recorded_first if count else None
        _require(count != 0 or recorded_first is None, 'First attack recorded with zero dispatches: ' + game_id)
        _require(not attacks or recorded_first is None or abs(recorded_first - first) < .000001,
                 'Native first attack/callback mismatch: ' + game_id)
        emit('attack_count', count, reason='complete_nonempty_ground_hunt_callbacks')
        emit('first_attack_observed', int(count > 0), reason='complete_nonempty_ground_hunt_callbacks')
    else:
        reason = 'missing_retained_ground_hunt_rows' if complete else 'uncertified_ground_hunt_stream'
        emit('attack_count', reason=reason)
        emit('first_attack_observed', reason=reason)
    event_time('first_attack_minute', first, certified and (count == 0 or first is not None),
               'exact_nonempty_ground_hunt_cycle')
    emit('army_value_time_mean', _mean(points, end, lambda point: _value(point, 'army_value')),
         reason='sampled_left_hold_actual_timeline_including_revivals')
    def army_present(point):
        value = _value(point, 'army_value')
        return None if value is None else int(value > 0)

    exposure = _mean(points, end, army_present)
    episodes = snapshots = None
    if exposure is not None:
        episodes = snapshots = 0
        previous = False
        for cycle, point in sorted(points.items()):
            present = bool(army_present(point))
            snapshots += int(present)
            episodes += int(present and not previous)
            previous = present
    emit('army_nonempty_minutes', exposure * end / CYCLES_PER_MINUTE if exposure is not None else None,
         reason='sampled_cycle_weighted_nonempty_army_exposure')
    emit('army_nonempty_episodes', episodes, reason='sampled_contiguous_nonempty_army_runs_not_independent_games')
    emit('army_nonempty_snapshots', snapshots, reason='observed_nonempty_army_samples_not_independent_games')

    def stock(metric, getter, applicability='all', summary=None):
        if applicability == 'city' and receipt['city'] is False:
            for minute in (-1,) + CHECKPOINTS:
                emit(metric, status='not_applicable', reason='city_simulation_disabled', minute=minute)
            return
        if applicability == 'city' and receipt['city'] is None:
            for minute in (-1,) + CHECKPOINTS:
                emit(metric, reason='city_applicability_unrecorded', minute=minute)
            return
        value = getter(final)
        if value is None and summary is not None:
            value = summary
        emit(metric, value, reason='native_final_stock' if value is not None else 'missing_native_stock_field')
        for minute in CHECKPOINTS:
            cycle = minute * CYCLES_PER_MINUTE
            if cycle in points:
                emit(metric, getter(points[cycle]), reason='exact_checkpoint_sample', minute=minute)
            elif cycle > end and defeated:
                emit(metric, 0, reason='native_declared_defeated_team_absorbing_postgame_zero', minute=minute)
            elif cycle > end:
                emit(metric, status='censored', reason='administrative_game_end_without_declared_defeat', minute=minute)
            else:
                emit(metric, reason='missing_exact_checkpoint', minute=minute)

    actual = native.get('actual') if native else None
    stock('army_value', lambda point: _value(point, 'army_value'))
    stock('army_units', lambda point: _value(point, 'army_units'))
    stock('harvesters', lambda point: _value(point, 'harvesters'), summary=(
        sum(int(actual.get(str(item), 0)) for item in (31, 59)) if actual is not None else None))
    stock('refineries', lambda point: _value(point, 'refineries'), summary=(
        int(actual.get('10', 0)) if actual is not None else None))
    stock('idle_money', _idle_money)
    emit('idle_money_time_mean', _mean(points, end, _idle_money), reason='sampled_idle_queue_and_no_upgrade_credits')
    for metric, item in (('zone_residential', 20), ('zone_industrial', 22), ('zone_commercial', 21)):
        stock(metric, lambda point, field=metric: _value(point, field), 'city',
              int(actual.get(str(item), 0)) if actual is not None else None)
    stock('population', lambda point: _value(point, 'population'), 'city')
    if run['research'] == 'off':
        emit('research_level', status='not_applicable', reason='research_off_or_unsupported_not_emulated')
    else:
        values = [_value(point, 'tech_level') for source in (points, evidence.tech_samples[(game_id, number)])
                  for point in source.values()]
        values = [value for value in values if value is not None]
        emit('research_level', max(values) if values else None, reason='maximum_observed_native_research_tech_level')
    stall, stall_reason = _stall_fraction(points)
    emit('economy_stall_fraction', stall,
         status='not_applicable' if stall_reason == 'no_eligible_base_exposure' else None, reason=stall_reason)
    losses = _value(final, 'structures_lost')
    built = _value(final, 'built')
    initial_built = _value(initial, 'built')
    if native:
        if losses is None and isinstance(native.get('lost'), dict):
            losses = sum(int(value) for item, value in native['lost'].items() if int(item) in STRUCTURE_ITEMS)
        if built is None and isinstance(native.get('built'), dict):
            built = sum(int(value) for item, value in native['built'].items() if int(item) in STRUCTURE_ITEMS)
            initial_built = 0
    initial_structures = _value(initial, 'structures')
    denominator = initial_structures + built - initial_built if all(value is not None for value in (
        initial_structures, built, initial_built)) else None
    emit('structures_lost', losses, reason='native_lifetime_structure_loss_ledger')
    emit('base_loss_fraction', losses / denominator if losses is not None and denominator is not None and denominator > 0 else None,
         reason='native_losses_over_initial_plus_completed_structures')
    for label, (field, item) in TECHNOLOGIES.items():
        built = _value(house, field + '_built')
        if built is None:
            built = _value(final, field + '_built')
        if built is None and native and isinstance(native.get('built'), dict):
            built = int(native['built'].get(str(item), 0))
        owned = [cycle for cycle, point in points.items() if (_value(point, field) or 0) > 0]
        exact_times = {}
        available = {}
        for kind in ('owned', 'completed'):
            raw = house.get('first_' + field + '_' + kind + '_cycle', final.get('first_' + field + '_' + kind + '_cycle'))
            cycle = _number(raw, 'first_' + field + '_' + kind + '_cycle')
            sidecar = _value(tech, 'first_' + kind + '_' + field + '_minute')
            available[kind] = cycle is not None or sidecar is not None
            exact_times[kind] = cycle / CYCLES_PER_MINUTE if cycle is not None and cycle >= 0 else sidecar
            if cycle is None and sidecar is None:
                # A zero native lifetime ledger proves no completion, not a
                # completion time when positive or an unobserved ownership.
                available[kind] = kind == 'completed' and built == 0
        native_owned = bool(native and isinstance(native.get('actual'), dict) and
                            int(native['actual'].get(str(item), 0)) > 0)
        if built is not None:
            adoption = int(built > 0 or native_owned or bool(owned) or any(time is not None for time in exact_times.values()))
        elif native_owned or owned or any(time is not None for time in exact_times.values()):
            adoption = 1
        else:
            adoption = None
        emit('adopt_' + label, adoption, reason='native_lifetime_build_or_positive_ownership_evidence')
        for kind in ('owned', 'completed'):
            event_time('first_' + kind + '_' + label + '_minute', exact_times[kind], available[kind], 'exact_native_first_' + kind + '_cycle')
        if owned:
            emit('first_sampled_owned_' + label + '_minute', min(owned) / CYCLES_PER_MINUTE,
                 reason='first_positive_sample_upper_bound_not_completion_time')
        else:
            emit('first_sampled_owned_' + label + '_minute', reason='no_positive_ownership_sample_not_a_first_time')
    built_values = []
    for label, item in SPECIAL_ITEMS.items():
        unit = evidence.units.get(((game_id, number), end), {}).get(item, {})
        built = _value(unit, 'built')
        if built is None and native and isinstance(native.get('built'), dict):
            built = int(native['built'].get(str(item), 0))
        built_values.append(built)
        emit(label + '_built', built, reason='native_original_house_lifetime_built_ledger')

    def share(cycle, items):
        denominator = _value(points.get(cycle), 'army_value')
        if denominator is None:
            return None, 'unavailable', 'missing_native_army_denominator'
        if denominator == 0:
            return None, 'not_applicable', 'no_live_army_denominator'
        numerators = []
        for item in items:
            unit = evidence.units.get(((game_id, number), cycle), {}).get(item)
            composition = evidence.composition.get(((game_id, number), cycle), {}).get(item)
            numerators.append(_value(unit or composition, 'live_value'))
        if any(value is None for value in numerators):
            return None, 'unavailable', 'missing_native_special_live_value'
        numerator = sum(numerators)
        if numerator > denominator or numerator < 0 or denominator < 0:
            return None, 'unavailable', 'native_share_denominator_mismatch'
        return numerator / denominator, 'measured', 'native_special_replacement_value_over_army'

    emit('special_built', sum(built_values) if all(value is not None for value in built_values) else None,
         reason='sum_native_devastator_deviator_sonic_lifetime_built')
    for metric, items in [('special_live_value_share', tuple(SPECIAL_ITEMS.values()))] + [
            (label + '_live_value_share', (item,)) for label, item in SPECIAL_ITEMS.items()]:
        value, status, reason = share(end, items)
        emit(metric, value, status=status, reason=reason)
        for minute in CHECKPOINTS:
            cycle = minute * CYCLES_PER_MINUTE
            if cycle > end:
                if defeated:
                    emit(metric, status='not_applicable',
                         reason='native_declared_defeated_team_no_postgame_army_denominator', minute=minute)
                else:
                    emit(metric, status='censored', reason='administrative_game_end_without_declared_defeat', minute=minute)
            else:
                value, status, reason = share(cycle, items)
                emit(metric, value, status=status, reason=reason, minute=minute)
    return rows


def scorecard(run):
    """Return validated row dictionaries without writing or changing input data.

    Every selected game is validated before any rows are returned. Invalid pins,
    options/maps, duplicate records, incomplete rosters and unfinished/error games
    raise ScorecardError; missing optional telemetry instead emits explicit rows.
    """
    evidence = _Evidence(run)
    rows = []
    for game_id in sorted(evidence.games):
        houses = evidence.houses[game_id]
        end = evidence.provenance[game_id]['end']
        eliminated = {number: evidence.terminal_cycle(house) for number, house in houses.items()
                      if int(house['alive']) == 0}
        teams = {number: int(house['team']) for number, house in houses.items()}
        inactive_teams = {team for team in teams.values()
                          if all(int(houses[number]['alive']) == 0 for number in houses if teams[number] == team)}
        ambiguous = len(inactive_teams) > 1 and any(
            time is None and teams[number] in inactive_teams for number, time in eliminated.items())
        # Placeholder times affect no surviving team's place/score. Suppress
        # every unidentified inactive-team rank instead of inventing tie ranks.
        placings = match_score.score_game(sorted(houses), {
            number: time if time is not None else end for number, time in eliminated.items()}, teams=teams)
        for house in sorted(houses):
            identified = not ambiguous or teams[house] not in inactive_teams
            rows.extend(_house_rows(evidence, game_id, house, placings, identified))
    return rows


def write_observations(path, rows):
    """Deterministic UTF-8 CSV/gzip (mtime=0, no host filename in gzip header)."""
    path = Path(path)
    with path.open('wb') as destination:
        with gzip.GzipFile(fileobj=destination, filename='', mode='wb', mtime=0) as compressed:
            with io.TextIOWrapper(compressed, encoding='utf-8', newline='') as text:
                writer = csv.DictWriter(text, fieldnames=COLUMNS, lineterminator='\n')
                writer.writeheader()
                writer.writerows(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--diagnostics', type=Path, help='Optional extraction status/counts JSON; never game telemetry')
    args = parser.parse_args(argv)
    try:
        manifest = _json_file(args.manifest)
        _require(manifest.get('schema_version') == SCHEMA_VERSION and isinstance(manifest.get('runs'), list)
                 and manifest['runs'], 'Invalid/empty scorecard manifest')
        rows, counts, identities = [], [], set()
        for raw_run in manifest['runs']:
            run = dict(raw_run)
            for field in ('results', 'fixtures', 'options_file', 'options_receipt'):
                if isinstance(run.get(field), str):
                    run[field] = str(_path(args.manifest.parent, run[field]))
            observed = scorecard(run)
            for row in observed:
                identity = tuple(row[field] for field in ('release', 'arm', 'id', 'research', 'house', 'metric', 'minute'))
                _require(identity not in identities, 'Duplicate observation across runs: ' + str(identity))
                identities.add(identity)
            rows.extend(observed)
            counts.append(dict(release=run['release'], arm=run['arm'], mod=run['mod'], research=run['research'],
                               rows=len(observed), results=run['results'],
                               genome_phases_sha256=run.get('genome_phases_sha256', ''),
                               statuses=dict(sorted(Counter(row['status'] for row in observed).items()))))
        # Do not create or truncate an observations file until all runs succeed.
        write_observations(args.output, rows)
        if args.diagnostics:
            args.diagnostics.write_text(json.dumps(dict(schema_version=SCHEMA_VERSION, status='extracted', runs=counts),
                                                   sort_keys=True, indent=2) + '\n', encoding='utf-8')
    except (ScorecardError, OSError, ValueError) as error:
        if args.diagnostics:
            args.diagnostics.write_text(json.dumps(dict(schema_version=SCHEMA_VERSION, status='rejected', error=str(error)),
                                                   sort_keys=True, indent=2) + '\n', encoding='utf-8')
        print('Scorecard provenance/completion rejection: ' + str(error), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
