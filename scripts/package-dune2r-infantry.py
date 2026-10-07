#!/usr/bin/env python3
"""Build a registered Dune2R Infantry atlas pack from an explicit local selection.

Offline authoring utility; Pillow is not a runtime or normal build dependency.
No provider requests, alpha cleaning, foreground cropping or asset fitting occur.
The source selection is local-only and must never be included in the public pack.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import tempfile

from PIL import Image

HEADINGS = ('east', 'north_east', 'north', 'north_west',
            'west', 'south_west', 'south', 'south_east')
STATES = ('Idle', 'Movement', 'Combat', 'CombatReturn',
          'DamageExploded', 'DamageAftermath')
SOURCE_SIZE = (1024, 1024)
FRAME_SIZE = (512, 512)
MAX_AXIS = 4096
MAX_PIXELS = 16 * 1024 * 1024
MAX_FRAMES = (MAX_AXIS // FRAME_SIZE[0]) * (MAX_AXIS // FRAME_SIZE[1])


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_hash(path: Path, expected: str) -> None:
    if len(expected) != 64 or sha256(path) != expected.lower():
        raise ValueError('Accepted source hash mismatch: ' + path.name)


def layout(count: int) -> tuple[int, int]:
    if not 1 <= count <= MAX_FRAMES:
        raise ValueError('Sequence cannot fit the bounded atlas')
    columns = min(MAX_AXIS // FRAME_SIZE[0], count)
    rows = math.ceil(count / columns)
    width, height = columns * FRAME_SIZE[0], rows * FRAME_SIZE[1]
    if width > MAX_AXIS or height > MAX_AXIS or width * height > MAX_PIXELS:
        raise ValueError('Atlas exceeds runtime texture limits')
    return columns, rows


def read_selection(path: Path) -> dict:
    selection = json.loads(path.read_text(encoding='utf-8'))
    if selection.get('schema') != 'dune2r-unit-runtime-source-v1':
        raise ValueError('Unsupported local selection schema')
    required = {'source_canvas': list(SOURCE_SIZE), 'frame_size': list(FRAME_SIZE),
                'item_id': 32, 'house_id': 0, 'base_size': [40, 40], 'scale': 1.0}
    if any(selection.get(key) != value for key, value in required.items()):
        raise ValueError('Incorrect Infantry presentation contract')
    anchor = selection.get('anchor', [])
    if len(anchor) != 2 or any(type(v) is not int or not 0 <= v < FRAME_SIZE[i]
                               for i, v in enumerate(anchor)):
        raise ValueError('Invalid shared frame anchor')
    expected_keys = {(state, heading) for state in STATES for heading in HEADINGS}
    actual_keys = []
    for sequence in selection.get('sequences', []):
        key = sequence.get('state'), sequence.get('direction')
        actual_keys.append(key)
        frames = sequence.get('frames', [])
        layout(len(frames))
        if sequence.get('loop') is not (key[0] in ('Idle', 'Movement', 'DamageAftermath')):
            raise ValueError('Incorrect one-shot/loop state')
        if key[0] in ('Idle', 'DamageAftermath') and len(frames) != 1:
            raise ValueError('Stationary states must be exact still frames')
        for frame in frames:
            if type(frame.get('duration_ms')) is not int or not 1 <= frame['duration_ms'] <= 60000:
                raise ValueError('Invalid authored frame duration')
            source_path = Path(frame['file']).resolve(strict=True)
            checked_hash(source_path, frame['sha256'])
            with Image.open(source_path) as image:
                if image.mode != 'RGBA' or image.size != SOURCE_SIZE:
                    raise ValueError('Every source must retain the complete RGBA 1024 canvas')
                alpha = image.getchannel('A')
                if alpha.getextrema()[0] != 0 or not alpha.getbbox():
                    raise ValueError('Source must have a nonempty transparent sprite')
    if len(actual_keys) != len(expected_keys) or set(actual_keys) != expected_keys:
        raise ValueError('Exactly one complete eight-direction set per state is required')
    for file, expected in selection.get('source_bindings', {}).items():
        checked_hash(Path(file).resolve(strict=True), expected)
    sequences = {(s['state'], s['direction']): s for s in selection['sequences']}
    for heading in HEADINGS:
        idle = sequences['Idle', heading]['frames'][0]['sha256']
        combat = sequences['Combat', heading]['frames']
        recovery = sequences['CombatReturn', heading]['frames']
        collapse = sequences['DamageExploded', heading]['frames']
        aftermath = sequences['DamageAftermath', heading]['frames'][0]['sha256']
        if combat[-1]['sha256'] != recovery[0]['sha256'] or recovery[-1]['sha256'] != idle:
            raise ValueError('Combat/recovery/idle endpoints are not exact')
        if collapse[0]['sha256'] != idle or collapse[-1]['sha256'] != aftermath:
            raise ValueError('Collapse/aftermath endpoints are not exact')
    return selection


def relative_atlas(sequence: dict) -> Path:
    return Path('atlases') / sequence['state'].lower() / (sequence['direction'] + '.png')


def make_atlas(sequence: dict) -> Image.Image:
    columns, rows = layout(len(sequence['frames']))
    atlas = Image.new('RGBA', (columns * FRAME_SIZE[0], rows * FRAME_SIZE[1]), (0, 0, 0, 0))
    for index, frame in enumerate(sequence['frames']):
        source_path = Path(frame['file'])
        checked_hash(source_path, frame['sha256'])
        with Image.open(source_path) as image:
            normalized = image.resize(FRAME_SIZE, Image.Resampling.NEAREST)
            # Unmasked paste preserves authored RGBA; masking would square the alpha.
            atlas.paste(normalized, ((index % columns) * FRAME_SIZE[0],
                                     (index // columns) * FRAME_SIZE[1]))
    return atlas


def manifest_text(selection: dict) -> str:
    lines = ['; Accepted registered Infantry art; full-canvas nearest 1024 to512.',
             '; Optional Dune2R graphics only; not a gameplay or collision definition.',
             '[Unit]', 'ItemID=32', 'HouseID=0', 'SourceUnit=harkonneninfantry', '',
             '[Render]', 'BaseWidth=40', 'BaseHeight=40', 'Scale=1.0', '']
    sequences = {(s['state'], s['direction']): s for s in selection['sequences']}
    for state in STATES:
        for heading in HEADINGS:
            sequence = sequences[state, heading]
            columns, rows = layout(len(sequence['frames']))
            durations = [f['duration_ms'] for f in sequence['frames']]
            lines.extend([f'[{state}.{heading}]',
                          f'Atlas={relative_atlas(sequence).as_posix()}',
                          f'Columns={columns}', f'Rows={rows}', f'Frames={len(durations)}',
                          f'FrameMs={durations[0]}',
                          'DurationsMs=' + ','.join(map(str, durations)),
                          f'AnchorX={selection["anchor"][0]}', f'AnchorY={selection["anchor"][1]}',
                          'Loop=' + str(sequence['loop']).lower(), ''])
    return '\n'.join(lines)


def verify_pack(selection: dict, destination: Path) -> dict:
    manifest = destination / 'unit.ini'
    if manifest.read_text(encoding='utf-8') != manifest_text(selection):
        raise ValueError('Runtime manifest does not match the accepted selection')
    expected_files = {'unit.ini'}
    output_hashes = {}
    for sequence in selection['sequences']:
        relative = relative_atlas(sequence)
        expected_files.add(relative.as_posix())
        path = destination / relative
        with Image.open(path) as actual:
            expected = make_atlas(sequence)
            if actual.mode != 'RGBA' or actual.size != expected.size or actual.tobytes() != expected.tobytes():
                raise ValueError('Runtime atlas pixels differ: ' + relative.as_posix())
        output_hashes[relative.as_posix()] = sha256(path)
    found = {p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
    if found != expected_files:
        raise ValueError('Runtime pack contains missing or unexpected files')
    source_digest = hashlib.sha256()
    for state in STATES:
        for heading in HEADINGS:
            sequence = next(s for s in selection['sequences'] if s['state'] == state and s['direction'] == heading)
            for index, frame in enumerate(sequence['frames']):
                source_digest.update(f'{state}.{heading}/{index}:{frame["sha256"]}:{frame["duration_ms"]}\n'.encode())
    pack_digest = hashlib.sha256()
    output_hashes['unit.ini'] = sha256(manifest)
    for relative, digest in sorted(output_hashes.items()):
        pack_digest.update(f'{relative}:{digest}\n'.encode())
    return {'sequences': len(selection['sequences']),
            'frames': sum(len(s['frames']) for s in selection['sequences']),
            'files': len(expected_files), 'bytes': sum((destination / f).stat().st_size for f in expected_files),
            'anchor': selection['anchor'], 'ordered_source_sha256': source_digest.hexdigest(),
            'ordered_pack_sha256': pack_digest.hexdigest(), 'output_hashes': output_hashes}


def write_pack(selection: dict, destination: Path) -> dict:
    destination = destination.resolve()
    if destination.name != 'harkonneninfantry' or destination.exists():
        raise ValueError('Write requires a NEW harkonneninfantry directory; never overwrite accepted assets')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Complete and verify a sibling staging pack before exposing unit.ini to the loader.
    with tempfile.TemporaryDirectory(prefix='infantry-stage-', dir=destination.parent) as staging:
        staged = Path(staging) / 'harkonneninfantry'
        staged.mkdir()
        for sequence in selection['sequences']:
            path = staged / relative_atlas(sequence)
            path.parent.mkdir(parents=True, exist_ok=True)
            make_atlas(sequence).save(path, compress_level=9)
        (staged / 'unit.ini').write_text(manifest_text(selection), encoding='utf-8', newline='\n')
        report = verify_pack(selection, staged)
        staged.rename(destination)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selection', required=True, type=Path, help='Explicit LOCAL-ONLY hash-bound source selection JSON')
    parser.add_argument('--output', type=Path, help='New runtime harkonneninfantry pack directory')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--write', action='store_true', help='Create a new validated pack (never replace one)')
    mode.add_argument('--check', action='store_true', help='Compare every shipped pixel and manifest against selected sources')
    args = parser.parse_args()
    selection = read_selection(args.selection)
    if args.write or args.check:
        if not args.output:
            parser.error('--output is required with --write or --check')
        report = write_pack(selection, args.output) if args.write else verify_pack(selection, args.output)
    else:
        report = {'validated': True, 'sequences': len(selection['sequences']),
                  'frames': sum(len(s['frames']) for s in selection['sequences']),
                  'anchor': selection['anchor'], 'writes': 0}
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
