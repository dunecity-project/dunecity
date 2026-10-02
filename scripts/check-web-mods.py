#!/usr/bin/env python3
"""Verify bundled mods in the actual Emscripten file table and data archive."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path, PurePosixPath
import re


REQUIRED = (
    '/mods/Tornie/mod.ini', '/mods/Tornie/manifest.json',
    '/mods/Tornie/checksums.sha256', '/mods/Tornie/ObjectData.ini',
    '/mods/Tornie/GameOptions.ini', '/mods/Tornie/QuantBot Config.ini',
    '/mods/Tornie/CustomHouse.ini', '/mods/Dune2R/mod.ini',
    '/mods/Dune2R/GameOptions.ini', '/mods/Dune2R/asset-catalog.ini',
)
CITY_MAPS = (
    '4P - 128x128 - 4 corners.ini', '4P - 192x192 - DuneCity.ini',
    '5P - 256x256 - test.ini', '8P - 128x128 - city seige.ini',
    'Alkozeltser 4 Cities.ini',
)
# Emscripten emits JSON properties before optimization and unquoted properties
# after optimization. Read only its file records; never execute generated JS.
FILE_RECORD = re.compile(
    r'\{\s*"?filename"?\s*:\s*("(?:\\.|[^"\\])*")\s*,\s*'
    r'"?start"?\s*:\s*(\d+(?:\.\d+)?(?:[eE]\+?\d+)?)\s*,\s*'
    r'"?end"?\s*:\s*(\d+(?:\.\d+)?(?:[eE]\+?\d+)?)\s*[,}]')


def check_payload(javascript, data, skin_payload=None):
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

    checked = set()
    manifest = files['/mods/Tornie/checksums.sha256'].decode('utf-8')
    for line in manifest.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match:
            raise ValueError('Invalid Tornie checksum entry')
        digest, relative = match.groups()
        path = PurePosixPath(relative)
        if path.is_absolute() or '..' in path.parts or relative in checked:
            raise ValueError('Invalid Tornie checksum path: ' + relative)
        checked.add(relative)
        name = '/mods/Tornie/' + relative
        if name not in files or hashlib.sha256(files[name]).hexdigest() != digest:
            raise ValueError('Missing or corrupt Tornie payload: ' + relative)
    if not checked:
        raise ValueError('Empty Tornie checksum manifest')
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
    return {'verified_tornie_files': len(checked),
            'verified_dunecity_skin_files': len(skin_payload),
            'verified_dunecity_maps': len(CITY_MAPS),
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
