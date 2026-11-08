#!/usr/bin/env python3
"""Read-only start-map features from exact exports and engine receipts.

Geometry ports RockBudget 4e1a962d7c581c7e03ba16b0a761f427e3d09cfc
(tests/ai/dunecity-audit.inc), not AI placement legality: four-neighbour rock,
including occupied rock/slabs but excluding mountains; free land excludes
structures, zones and accepted reservations, and ignores units. Rectangles
count overlapping top-left origins, NEVER disjoint building capacity. Global
rows use house=''; house rows survey the lowest-ID yard's formation (an MCV
anchor supplies export-only topology when no yard exists). Largest-component
ties use the first row-major component. Distances are Euclidean tile distances
between primary yard origins, falling back to primary MCV origins, not paths.
Densities divide tile counts by width*height; blooms are not harvestable spice.

Exports describe map input, not necessarily engine cycle zero: CY starts deploy
MCVs after generating the INI. Their free/origin counts remain blank until
engine receipts supply them. Export spice amounts and accepted reservations
are unknowable from terrain characters and remain blank. Player<N> sections
are never guessed from roster order: retained loader binding receipts resolve
house identity. Global base distances do not require that binding.

API: MAP_FEATURE_FIELDS; rows_from_log(lines, game, setup=None);
rows_from_map(data: bytes, game, setup=None); collect_results(directories).
All return the same schema. Engine-start nonblank fields take precedence over
exports. measurement_status names sources and partial/predeploy limitations.
evidence_sha256 is the exact export SHA or SHA256 of consumed receipt text
(with logger prefixes removed, newline terminated); mixed sources hash their
sorted, unique evidence hashes, newline terminated. No output is written until
all export chunks, lengths, hashes and available engine map hashes validate.
"""

import argparse
import base64
import binascii
import configparser
import csv
import gzip
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path, PurePosixPath
import re
import tarfile

MAP_FEATURE_FIELDS = (
    'id', 'house', 'map_hash', 'size', 'player_count', 'initial_spice_density',
    'rock_area', 'rock_components', 'largest_rock_area', 'free3x3_count',
    'largest_component_free3x3', 'mountain_density', 'base_distance_min',
    'base_distance_mean', 'measurement_status', 'evidence_sha256',
    'width', 'height', 'cycle', 'initial_spice_amount', 'rock_initial_area',
    'rock_initial_free', 'rock_free', 'rock_in_range', 'rock_committed',
    'free2x2_count', 'free3x2_count', 'in_range2x2_count', 'in_range3x2_count',
    'in_range3x3_count', 'base_x', 'base_y',
)
_FLOAT_FIELDS = {'initial_spice_density', 'initial_spice_amount', 'mountain_density',
                 'base_distance_min', 'base_distance_mean'}
_METRICS = set(MAP_FEATURE_FIELDS) - {'id', 'house', 'map_hash', 'size',
                                   'measurement_status', 'evidence_sha256'}
_HOUSES = {name: index for index, name in enumerate((
    'harkonnen', 'atreides', 'ordos', 'fremen', 'sardaukar', 'mercenary',
    'neutral', 'rebels', 'custom', 'wildspade', 'kleshmersh', 'tharpique'))}
# src/sand.cpp getStructureSize/getItemIDByName at the RockBudget source pin.
# Concrete and Road are tile substrates, not occupying StructureBase objects.
_DIMENSIONS = {}
for _size, _names in (
    ((1, 1), ('turret', 'gun-turret', 'r-turret', 'rocket-turret', 'wall', 'concrete',
              'slab1', 'road', 'power line', 'powerline', 'scoutpost', 'scout post',
              'green post', 'sentinel post', 'avant-poste', 'avant poste', 'flamepost',
              'flame post', 'slowpost', 'chemipost', 'chemi post')),
    ((2, 2), ('barracks', 'const yard', 'construction yard', 'ix', 'house ix',
              'light fctry', 'light factory', 'outpost', 'radar', 'spice silo',
              'silo', 'slab4', 'windtrap', 'wor', 'police station', 'police',
              'zone residential', 'residential zone', 'zone commercial',
              'commercial zone', 'zone industrial', 'industrial zone')),
    ((3, 2), ('heavy fctry', 'heavy factory', 'hi-tech', 'hightech factory',
              'refinery', 'repair', 'repair yard', 'advanced windtrap mk3',
              'advanced wind trap mk3', 'advanced windtrap 3x2',
              'advanced wind trap 3x2', 'worfinery', 'tech center', 'techcenter',
              'research lab', 'chaos factory', 'chaosfactory')),
    ((3, 3), ('palace', 'star port', 'starport', 'nuclear', 'nuclear plant',
              'stadium', 'airport', 'advanced windtrap', 'advanced wind trap',
              'advanced windtrap 3x3', 'advanced wind trap 3x3')),
    ((2, 3), ('advanced windtrap mk2', 'advanced wind trap mk2',
              'advanced windtrap 2x3', 'advanced wind trap 2x3',
              'love factory', 'lovefactory')),
    ((5, 2), ('doublefinery',)),
):
    _DIMENSIONS.update(dict.fromkeys(_names, _size))
_RECTANGLES = ((2, 2), (3, 2), (3, 3))
_KEY_RECTANGLE = {'ix': (2, 2), 'heavy': (3, 2), 'repair': (3, 2),
                  'hitech': (3, 2), 'refinery': (3, 2),
                  'starport': (3, 3), 'palace': (3, 3)}


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _combined_sha(hashes):
    hashes = sorted(set(filter(None, hashes)))
    return hashes[0] if len(hashes) == 1 else _sha(('\n'.join(hashes) + '\n').encode()) if hashes else ''


def _parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def _house(value):
    text = str(value).strip().lower()
    if text in _HOUSES:
        return str(_HOUSES[text])
    return str(int(text)) if text.lstrip('-').isdigit() and int(text) >= 0 else ''


def _roster(game):
    sides = game.get('sides', ())
    if isinstance(sides, str):
        sides = [side.split(':') for side in sides.split(',') if side]
    return [_house(side[0]) for side in sides if side and _house(side[0]) != '']


def _row(game, setup, house=''):
    row = dict.fromkeys(MAP_FEATURE_FIELDS, '')
    row.update(id=game.get('id', ''), house=house,
               map_hash=setup.get('map_hash') or game.get('map_hash', ''))
    row['size'] = game.get('size', '')
    roster = _roster(game)
    if roster:
        row['player_count'] = len(roster)
    return row


def _assign(row, data):
    for field in _METRICS.intersection(data):
        raw = data[field]
        if raw in ('', None):
            continue
        value = float(raw) if field in _FLOAT_FIELDS else int(raw)
        if not math.isfinite(value):
            raise ValueError(f'Non-finite {field} in map receipt')
        if value >= 0:
            row[field] = value
    if row['width'] != '' and row['height'] != '':
        row['size'] = str(row['width']) if row['width'] == row['height'] else f"{row['width']}x{row['height']}"


def setup_from_log(lines, setup=None):
    """Return setup plus exact Player<N>-to-house bindings, without mutation."""
    resolved = dict(setup or {})
    sections = dict(resolved.get('house_sections', {}))
    evidence = []
    for line in lines:
        if 'SCENARIO_SETUP:' in line:
            parsed = _parse(line, 'SCENARIO_SETUP:')
            if resolved.get('map_hash') and parsed.get('map_hash') != resolved['map_hash']:
                raise ValueError('Raw log and game table have different map hashes')
            resolved.update(parsed)
        if 'INIMapLoader: player setup ' in line:
            data = _parse(line, 'INIMapLoader: player setup ')
            house = _house(data.get('resolvedHouse', ''))
            if house != '' and data.get('section'):
                sections[data['section'].lower()] = house
                evidence.append(line[line.index('INIMapLoader:'):].rstrip() + '\n')
    if sections:
        resolved['house_sections'] = sections
    if evidence:
        resolved['section_evidence_sha256'] = _sha(''.join(evidence).encode())
    return resolved


def rows_from_log(lines, game, setup=None):
    """Parse cycle==0 new markers and RockBudget ROCK/ROCK_KEY receipts.

    Parent audit hook: append(results/'map-features.csv', MAP_FEATURE_FIELDS,
                              rows_from_log(lines, game, setup)).
    Negative engine sentinels and all absent measurements stay blank.
    """
    rows, evidence, complete = {}, defaultdict(list), set()
    setup = dict(setup or {})
    for line in lines:
        marker = next((name for name in ('DUNECITY_MAP_FEATURE:', 'DUNECITY_ROCK:',
                                        'DUNECITY_ROCK_KEY:') if name in line), None)
        if not marker:
            continue
        data = _parse(line, marker)
        if data.get('cycle') != '0':
            continue
        house = _house(data.get('house', ''))
        if house == '' and marker != 'DUNECITY_MAP_FEATURE:':
            continue
        row = rows.setdefault(house, _row(game, setup, house))
        if marker == 'DUNECITY_MAP_FEATURE:':
            _assign(row, data)
            complete.add(house)
        elif marker == 'DUNECITY_ROCK:':
            _assign(row, {field: data[field] for field in (
                'cycle', 'rock_area', 'rock_free', 'rock_initial_area',
                'rock_initial_free', 'rock_in_range', 'rock_committed') if field in data})
        elif data.get('name') in _KEY_RECTANGLE:
            width, height = _KEY_RECTANGLE[data['name']]
            for source, target in (('origins', f'free{width}x{height}_count'),
                                   ('range_origins', f'in_range{width}x{height}_count')):
                if source in data:
                    old = row[target]
                    _assign(row, {target: data[source]})
                    if old != '' and old != row[target]:
                        raise ValueError(f'Conflicting {target} at cycle zero for {game.get("id")}/{house}')
            row['cycle'] = 0
        evidence[house].append(line[line.index(marker):].rstrip() + '\n')
    for house, row in rows.items():
        row['measurement_status'] = 'engine_start' if house in complete else 'engine_start_partial'
        row['evidence_sha256'] = _sha(''.join(evidence[house]).encode())
    return [rows[house] for house in sorted(rows, key=lambda value: int(value) if value else -1)]


def _fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f'{value:016x}'


def decode_export_rows(rows):
    """Validate every artifact's chunk metadata/base64/length/SHA256 before use."""
    groups = defaultdict(dict)
    metadata = {}
    for row in rows:
        key = row['id'], row['artifact']
        descriptor = row['sha256'], int(row['bytes']), int(row['chunks'])
        index = int(row['chunk'])
        if descriptor[1] < 0 or descriptor[2] <= 0 or not 0 <= index < descriptor[2]:
            raise ValueError(f'Invalid export chunk metadata: {key}')
        if key in metadata and metadata[key] != descriptor:
            raise ValueError(f'Inconsistent export metadata: {key}')
        metadata[key] = descriptor
        try:
            chunk = base64.b64decode(row['base64'], validate=True)
        except (ValueError, binascii.Error) as exc:
            raise ValueError(f'Invalid export base64: {key}/{index}') from exc
        if index in groups[key] and groups[key][index] != chunk:
            raise ValueError(f'Conflicting export chunk: {key}/{index}')
        groups[key][index] = chunk
    decoded = {}
    for key, chunks in groups.items():
        digest, length, count = metadata[key]
        if set(chunks) != set(range(count)):
            raise ValueError(f'Missing export chunks: {key}')
        data = b''.join(chunks[index] for index in range(count))
        if len(data) != length or _sha(data) != digest:
            raise ValueError(f'Export bytes/SHA256 mismatch: {key}')
        decoded[key] = data
    return decoded


def _components(rock, width, height):
    # RockBudget sampleRock: occupied tiles remain in the four-neighbour flood.
    labels, groups = [-1] * len(rock), []
    for seed, is_rock in enumerate(rock):
        if not is_rock or labels[seed] >= 0:
            continue
        group = [seed]
        labels[seed] = len(groups)
        for at in group:
            x, y = at % width, at // width
            neighbours = ((at - 1 if x else -1), (at + 1 if x + 1 < width else -1),
                          (at - width if y else -1), (at + width if y + 1 < height else -1))
            for candidate in neighbours:
                if candidate >= 0 and rock[candidate] and labels[candidate] < 0:
                    labels[candidate] = len(groups)
                    group.append(candidate)
        groups.append(group)
    return labels, groups


def _origins(free, width, height, shape):
    sx, sy = shape
    return [y * width + x for y in range(height - sy + 1) for x in range(width - sx + 1)
            if all(free[(y + dy) * width + x + dx] for dy in range(sy) for dx in range(sx))]


def _distances(anchors, owner=None):
    if len(anchors) < 2 or any(value is None for value in anchors.values()):
        return '', ''
    keys = sorted(anchors)
    values = [math.dist(anchors[a], anchors[b]) for i, a in enumerate(keys) for b in keys[i + 1:]
              if owner is None or owner in (a, b)]
    return (min(values), sum(values) / len(values)) if values else ('', '')


def rows_from_map(data, game, setup=None):
    """Measure exact exported INI; never simulate startup or guess seat bindings."""
    setup = dict(setup or {})
    digest = _sha(data)
    map_hash = setup.get('map_hash') or game.get('map_hash', '')
    algorithm = setup.get('map_hash_algorithm', '')
    if map_hash and (algorithm == 'fnv1a64' or re.fullmatch('[0-9a-fA-F]{16}', map_hash)):
        if _fnv1a64(data) != map_hash.lower():
            raise ValueError(f'Export/engine map hash mismatch for {game.get("id")}')
    if algorithm == 'sha256' and digest != map_hash:
        raise ValueError(f'Export/engine map SHA256 mismatch for {game.get("id")}')
    setup['map_hash'] = map_hash or _fnv1a64(data)
    ini = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(';',))
    ini.read_string(data.decode('utf-8-sig'))
    sections = {section.lower(): dict(ini[section]) for section in ini.sections()}
    terrain = sections.get('map', {})
    width, height = int(terrain.get('sizex', 0)), int(terrain.get('sizey', 0))
    global_row = _row(game, setup)
    global_row.update(measurement_status='map_export_partial', evidence_sha256=digest)
    roster = _roster(game)
    players = [name for name in sections if re.fullmatch(r'player\d+', name) or name in _HOUSES]
    player_count = len(roster) if roster else len(players)
    global_row['player_count'] = player_count
    if width <= 0 or height <= 0:
        return [global_row]
    _assign(global_row, dict(width=width, height=height))
    tiles = ''.join(terrain.get(f'{y:03}', '') for y in range(height))
    if (any(len(terrain.get(f'{y:03}', '')) != width for y in range(height))
            or set(tiles) - set('-^~+gGbrRBlLiwWx%@OQ')):
        return [global_row]
    rock = [tile == '%' for tile in tiles]
    occupied, known_occupancy = set(), True
    anchors, footprints = {}, {}
    objects = defaultdict(list)
    for section in ('structures', 'units'):
        for key, value in sections.get(section, {}).items():
            parts = [part.strip() for part in value.split(',')]
            if key.lower().startswith('gen') and section == 'structures' and len(parts) >= 2:
                position, owner, name, health = int(key[3:]), parts[0].lower(), parts[1].lower(), 256
            elif key.lower().startswith('id') and len(parts) >= 4:
                owner, name, health, position = parts[0].lower(), parts[1].lower(), int(parts[2]), int(parts[3])
            else:
                known_occupancy = False
                continue
            if health <= 0:
                continue
            if not 0 <= position < width * height:
                known_occupancy = False
                continue
            identity = int(key[2:]) if key[2:].isdigit() else 0
            if section == 'units':
                if name == 'mcv':
                    objects[owner].append((1, identity, position, (1, 1)))
                continue  # Units never consume rock capacity.
            shape = _DIMENSIONS.get(name)
            if not shape:
                known_occupancy = False
                continue
            x, y = position % width, position // width
            if x + shape[0] > width or y + shape[1] > height:
                known_occupancy = False
                continue
            footprint = [(y + dy) * width + x + dx for dy in range(shape[1]) for dx in range(shape[0])]
            if name in ('concrete', 'slab1', 'slab4'):
                for at in footprint:
                    rock[at] = True
            elif name != 'road':
                occupied.update(footprint)
            if name in ('const yard', 'construction yard'):
                objects[owner].append((0, identity, position, shape))
    for owner in players:
        choices = sorted(objects.get(owner, ()))
        if choices:
            kind, identity, position, shape = choices[0]
            anchors[owner] = (position % width, position // width)
            footprints[owner] = [(position // width + dy) * width + position % width + dx
                                 for dy in range(shape[1]) for dx in range(shape[0])]
        else:
            anchors[owner] = None
    # Input MCVs may already have become occupied yards at engine cycle zero.
    predeploy = game.get('base') == 'cy' and any(choice[0] == 1 for choices in objects.values() for choice in choices)
    free_known = known_occupancy and not predeploy
    free = [is_rock and at not in occupied for at, is_rock in enumerate(rock)]
    labels, groups = _components(rock, width, height)
    origins = {shape: _origins(free, width, height, shape) for shape in _RECTANGLES} if free_known else {}
    largest = max(range(len(groups)), key=lambda index: len(groups[index])) if groups else None
    global_row.update(initial_spice_density=sum(tile in '~+gGrRlLwW' for tile in tiles) / len(tiles),
                      mountain_density=tiles.count('@') / len(tiles), rock_area=sum(rock),
                      rock_components=len(groups), largest_rock_area=len(groups[largest]) if groups else 0)
    if free_known:
        global_row['rock_free'] = sum(free)
        for shape, values in origins.items():
            global_row[f'free{shape[0]}x{shape[1]}_count'] = len(values)
        global_row['largest_component_free3x3'] = sum(labels[at] == largest for at in origins[(3, 3)])
    if len(anchors) == player_count:
        global_row['base_distance_min'], global_row['base_distance_mean'] = _distances(anchors)
    status = 'map_export_predeploy' if predeploy else 'map_export' if known_occupancy else 'map_export_partial'
    global_row['measurement_status'] = status
    binding = {name: _house(name) for name in players if name in _HOUSES}
    binding.update({str(name).lower(): _house(house) for name, house in setup.get('house_sections', {}).items()})
    inverse = {house: section for section, house in binding.items() if house != ''}
    result = [global_row]
    for house in roster or sorted(inverse, key=int):
        row = _row(game, setup, house)
        for field in ('size', 'width', 'height', 'player_count', 'initial_spice_density', 'mountain_density'):
            row[field] = global_row[field]
        row.update(measurement_status=status, evidence_sha256=digest)
        owner = inverse.get(house)
        if owner not in footprints:
            row['measurement_status'] += '_unbound'
            result.append(row)
            continue
        row['evidence_sha256'] = _combined_sha((digest, setup.get('section_evidence_sha256', '')))
        row['base_x'], row['base_y'] = anchors[owner]
        if len(anchors) == player_count:
            row['base_distance_min'], row['base_distance_mean'] = _distances(anchors, owner)
        selected = {labels[at] for at in footprints[owner] if labels[at] >= 0}
        local_largest = max(sorted(selected), key=lambda index: len(groups[index])) if selected else None
        row.update(rock_area=sum(len(groups[index]) for index in selected), rock_components=len(selected),
                   largest_rock_area=len(groups[local_largest]) if selected else 0)
        if free_known:
            row['rock_free'] = sum(free[at] for index in selected for at in groups[index])
            for shape, values in origins.items():
                row[f'free{shape[0]}x{shape[1]}_count'] = sum(labels[at] in selected for at in values)
            row['largest_component_free3x3'] = sum(labels[at] == local_largest for at in origins[(3, 3)])
        result.append(row)
    return result


def _open_text(path):
    return gzip.open(path, 'rt', encoding='utf-8', newline='') if path.suffix == '.gz' else path.open(encoding='utf-8', newline='')


def _table(directory, name):
    path = directory / name
    if not path.is_file():
        path = directory / (name + '.gz')
    if not path.is_file():
        return []
    with _open_text(path) as handle:
        return list(csv.DictReader(handle))


def _log_sources(directory, games):
    tokens = {'g' + _sha(identifier.encode())[:12]: identifier for identifier in games}
    for identifier, game in games.items():
        relative = game.get('diagnostics') or f'diagnostics/g{_sha(identifier.encode())[:12]}'
        for filename in ('run.log', 'run.log.gz'):
            path = directory / relative / filename
            if path.is_file():
                with _open_text(path) as handle:
                    yield identifier, handle.read().splitlines()
                break
    archive = directory / 'probe-evidence.tar.gz'
    if archive.is_file():
        with tarfile.open(archive, 'r:gz') as handle:
            for member in handle:
                path = PurePosixPath(member.name)
                if member.isfile() and path.name == 'run.log' and path.parent.name in tokens:
                    with handle.extractfile(member) as source:
                        yield tokens[path.parent.name], source.read().decode('utf-8', errors='replace').splitlines()


def merge_feature_rows(rows):
    """Combine source rows on id,house,map_hash, preferring measured engine data."""
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row['id'], str(row['house']), row['map_hash'])].append(row)
    result = []
    for key, sources in sorted(grouped.items()):
        sources.sort(key=lambda row: (row['measurement_status'].startswith('engine_start'),
                                      row['measurement_status'] == 'engine_start'))
        merged = dict.fromkeys(MAP_FEATURE_FIELDS, '')
        for source in sources:
            for field in MAP_FEATURE_FIELDS:
                if source.get(field, '') != '' and field not in ('measurement_status', 'evidence_sha256'):
                    merged[field] = source[field]
        statuses = {source['measurement_status'] for source in sources}
        has_export = any(status.startswith('map_export') for status in statuses)
        has_engine = any(status.startswith('engine_start') for status in statuses)
        if 'engine_start' in statuses:
            merged['measurement_status'] = 'engine_start' + ('+map_export' if has_export else '')
        elif has_engine and has_export:
            limitations = sorted(status for status in statuses if status.startswith('map_export'))
            merged['measurement_status'] = 'engine_start_partial+' + '+'.join(limitations)
        else:
            merged['measurement_status'] = '+'.join(sorted(statuses))
        merged['evidence_sha256'] = _combined_sha(source['evidence_sha256'] for source in sources)
        # A predeploy MCV tile is not proof that the eventual yard touched only
        # that component. Do not retain mismatched dependent export statistics.
        if key[1] and has_engine and any(source['rock_area'] != '' and
                source['rock_area'] != merged['rock_area'] for source in sources):
            for field in ('rock_components', 'largest_rock_area', 'largest_component_free3x3'):
                measured = [source[field] for source in sources
                            if source['measurement_status'].startswith('engine_start') and source[field] != '']
                merged[field] = measured[-1] if measured else ''
        result.append(merged)
    return result


def collect_results(directories):
    """Read direct or nested results, including collected RockBudget archives."""
    source_dirs = set()
    for root in map(Path, directories):
        if not root.is_dir():
            raise ValueError(f'Results directory does not exist: {root}')
        source_dirs.add(root)
        for name in ('games.csv', 'games.csv.gz', 'export-maps.csv', 'export-maps.csv.gz'):
            source_dirs.update(path.parent for path in root.rglob(name))
    all_rows = []
    for directory in sorted(source_dirs):
        games = {row['id']: row for row in _table(directory, 'games.csv')}
        artifacts = decode_export_rows(_table(directory, 'export-maps.csv'))
        setups = {}
        for (identifier, name), data in artifacts.items():
            if name == 'provenance.json':
                provenance = json.loads(data)
                games[identifier] = dict(games.get(identifier, {}), **provenance.get('game', {}))
                setups[identifier] = dict(provenance.get('setup', {}))
        for identifier, game in games.items():
            setups.setdefault(identifier, dict(map_hash=game.get('map_hash', '')))
        for identifier, lines in _log_sources(directory, games):
            setup = setup_from_log(lines, setups[identifier])
            setups[identifier] = setup
            all_rows.extend(rows_from_log(lines, games[identifier], setup))
        for (identifier, name), data in artifacts.items():
            if name == 'map.ini':
                game = games.get(identifier, dict(id=identifier))
                all_rows.extend(rows_from_map(data, game, setups.get(identifier, {})))
        for sample in _table(directory, 'samples.csv'):
            if sample.get('cycle') != '0' or not any(sample.get(field, '') != '' for field in
                                                    ('rock_area', 'rock_initial_area', 'rock_initial_free')):
                continue
            identifier = sample['id']
            game = games.get(identifier, sample)
            row = _row(game, setups.get(identifier, {}), _house(sample['house']))
            _assign(row, {field: value for field, value in sample.items()
                          if field in ('cycle', 'rock_area', 'rock_free', 'rock_initial_area',
                                       'rock_initial_free', 'rock_in_range', 'rock_committed')})
            row['measurement_status'] = 'engine_start_partial'
            row['evidence_sha256'] = _sha((json.dumps(sample, sort_keys=True, separators=(',', ':')) + '\n').encode())
            all_rows.append(row)
        for row in _table(directory, 'map-features.csv'):
            _assign(row, {field: row[field] for field in _METRICS if field in row})
            all_rows.append({field: row.get(field, '') for field in MAP_FEATURE_FIELDS})
    return merge_feature_rows(all_rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', nargs='+', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    rows = collect_results(args.results)
    with args.output.open('w', encoding='utf-8', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=MAP_FEATURE_FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
