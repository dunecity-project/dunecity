#!/usr/bin/env python3
"""Verify all six Tornie-family mods and DuneCity content in the browser archive."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path, PurePosixPath
import re


FAMILY_MODS = ('Tornie', 'TornieLite', 'Jericho', 'JerichoLite', 'ArcaKev', 'ArcaKevLite')
REQUIRED = tuple(
    f'/mods/{mod}/{relative}'
    for mod in FAMILY_MODS
    for relative in ('mod.ini', 'manifest.json', 'checksums.sha256', 'ObjectData.ini',
                     'GameOptions.ini', 'QuantBot Config.ini', 'CustomHouse.ini')
) + (
    '/mods/Dune2R/mod.ini', '/mods/Dune2R/GameOptions.ini', '/mods/Dune2R/asset-catalog.ini',
)
FACTION_FRAMES = tuple(
    f'CampaignHerald{house}.png' for house in (
        'Harkonnen', 'Atreides', 'Ordos', 'Fremen', 'Sardaukar', 'Mercenary',
        'Neutral', 'Rebels', 'Corruptique', 'Wildspade', 'Kleshmersh', 'Tharpique')
)
CITY_MAPS = (
    '4P - 128x128 - 4 corners.ini', '4P - 192x192 - DuneCity.ini',
    '5P - 256x256 - test.ini', '8P - 128x128 - city seige.ini',
    'Alkozeltser 4 Cities.ini',
    '2P - 128x128 Four Quadrants.ini', '4P - 192x192 - BigCityLife.ini',
    '4P - 192x192 - New Beginning.ini', '4P - 384x384 - new beginning.ini',
    '8P - 256x256 - Colonists.ini', '8P - 384x384 - Colonist.ini',
    '3P - 192x192 - 3waysplit.ini', '4P - 448x448 - No Mercy.ini',
)
CITY_MAP_METADATA = tuple(name + '.workshop.ini' for name in CITY_MAPS
                          if name != 'Alkozeltser 4 Cities.ini')
# Emscripten emits JSON properties before optimization and unquoted properties
# after optimization. Read only its file records; never execute generated JS.
FILE_RECORD = re.compile(
    r'\{\s*"?filename"?\s*:\s*("(?:\\.|[^"\\])*")\s*,\s*'
    r'"?start"?\s*:\s*(\d+(?:\.\d+)?(?:[eE]\+?\d+)?)\s*,\s*'
    r'"?end"?\s*:\s*(\d+(?:\.\d+)?(?:[eE]\+?\d+)?)\s*[,}]')


def check_payload(javascript, data, skin_payload=None, frame_payload=None):
    files = {}
    for encoded, start, end in FILE_RECORD.findall(javascript):
        name = json.loads(encoded)
        start, end = Decimal(start), Decimal(end)
        if start != start.to_integral_value() or end != end.to_integral_value():
            raise ValueError('Non-integer preload range: ' + name)
        if name in files or not 0 <= start <= end <= len(data):
            raise ValueError('Invalid or duplicate preload range: ' + name)
        start, end = int(start), int(end)
        files[name] = data[start:end]
    for name in REQUIRED:
        if not files.get(name):
            raise ValueError('Missing bundled mod file: ' + name)
    for name in files:
        if name.startswith(('/mods/Dune2R/graphics_hd/units/',
                            '/mods/Dune2R/graphics_compact/objpics/')):
            raise ValueError('Optional Dune2R art must download separately: ' + name)

    verified_family_files = {}
    for mod in FAMILY_MODS:
        checked = set()
        manifest = files[f'/mods/{mod}/checksums.sha256'].decode('utf-8')
        for line in manifest.splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
            if not match:
                raise ValueError(f'Invalid {mod} checksum entry')
            digest, relative = match.groups()
            path = PurePosixPath(relative)
            if path.is_absolute() or '..' in path.parts or relative in checked:
                raise ValueError(f'Invalid {mod} checksum path: ' + relative)
            checked.add(relative)
            name = f'/mods/{mod}/' + relative
            if name not in files or hashlib.sha256(files[name]).hexdigest() != digest:
                raise ValueError(f'Missing or corrupt {mod} payload: ' + relative)
        if not checked:
            raise ValueError(f'Empty {mod} checksum manifest')
        verified_family_files[mod] = len(checked)
    if frame_payload is None:
        frame_root = Path(__file__).resolve().parents[1] / 'data/frames'
        frame_payload = {name: (frame_root / name).read_bytes() for name in FACTION_FRAMES}
    for relative, expected in frame_payload.items():
        if files.get('/frames/' + relative) != expected:
            raise ValueError('Missing or corrupt shared faction frame: ' + relative)
    if skin_payload is None:
        skin_root = Path(__file__).resolve().parents[1] / 'mods/dunecity/graphics_skins'
        skin_payload = {p.relative_to(skin_root).as_posix(): p.read_bytes()
                        for p in skin_root.rglob('*') if p.is_file()}
    if not skin_payload:
        raise ValueError('Empty DuneCity skin source payload')
    for relative, expected in skin_payload.items():
        name = '/mods/dunecity/graphics_skins/' + relative
        if files.get(name) != expected:
            raise ValueError('Missing or corrupt DuneCity skin payload: ' + relative)
    map_root = Path(__file__).resolve().parents[1] / 'data/maps/singleplayer'
    for relative in CITY_MAPS:
        name = '/maps/singleplayer/' + relative
        if files.get(name) != (map_root / relative).read_bytes():
            raise ValueError('Missing or corrupt DuneCity map payload: ' + relative)
    for relative in CITY_MAP_METADATA:
        name = '/maps/singleplayer/' + relative
        if files.get(name) != (map_root / relative).read_bytes():
            raise ValueError('Missing or corrupt DuneCity map metadata: ' + relative)
    return {'verified_family_files': verified_family_files,
            'verified_shared_faction_frames': len(frame_payload),
            'family_payload_bytes': {
                mod: sum(len(value) for name, value in files.items() if name.startswith(f'/mods/{mod}/'))
                for mod in FAMILY_MODS},
            'shared_faction_frame_bytes': sum(map(len, frame_payload.values())),
            'verified_dunecity_skin_files': len(skin_payload),
            'verified_dunecity_maps': len(CITY_MAPS),
            'verified_dunecity_map_metadata': len(CITY_MAP_METADATA),
            'dune2r_files': sum(n.startswith('/mods/Dune2R/') for n in files),
            'data_bytes': len(data)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', required=True, type=Path)
    args = parser.parse_args()
    output = args.build_root / 'bin'
    result = check_payload((output / 'dunecity.js').read_text(),
                           (output / 'dunecity.data').read_bytes())
    print('Browser bundled mods verified: ' + json.dumps(result))
