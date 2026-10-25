#!/usr/bin/env python3
"""Render a city airplane flight traced by city_aircraft_probe as frame strips.

city_aircraft_probe writes one row per game cycle (flight-path-<mod>.csv,
straight-legs-<mod>.csv): the plane's world position and the sheet cell the
renderer draws (column drawn_angle, row drawn_frame). This draws exactly those
cells from the airplane sheet the build shipped:

  * the track: the flight's path at zoom 0 (16 px per tile, the sprite's own
    scale), with the drawn cell stamped every --every-tiles tiles of travel;
  * the strip: the drawn cell every --every-cycles cycles, left to right.

Pass the sheet the trace was made with: an 8x4 sheet from before the
in-between headings, or the current 8x5 one. Pillow is an authoring dependency.

    python3 tests/units/city-airplane-frame-strip.py --trace flight-path-dunecity.csv \
        --sheet imported_sprites/micropolis/aircraft/city_airplane.png --out after.png
"""
import argparse
import csv
import math
from pathlib import Path

from PIL import Image, ImageDraw

CELL = 48
WORLD_PER_PIXEL = 4          # TILESIZE 64 world units = 16 px at zoom 0
SAND = (196, 164, 112, 255)
PATH = (120, 40, 40, 255)


def load_rows(path, leg):
    with open(path, newline='') as handle:
        rows = [row for row in csv.DictReader(handle) if leg is None or int(row['leg']) == leg]
    if not rows:
        raise SystemExit('no trace rows in %s' % path)
    return rows


def cell(sheet, row):
    column, frame = int(row['drawn_angle']), int(row['drawn_frame'])
    return sheet.crop((column * CELL, frame * CELL, column * CELL + CELL, frame * CELL + CELL))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--sheet', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--leg', type=int, help='only this leg of a straight-legs trace')
    parser.add_argument('--title', default='')
    parser.add_argument('--every-tiles', type=float, default=3.0)
    parser.add_argument('--every-cycles', type=int, default=16)
    parser.add_argument('--strip-frames', type=int, default=40)
    args = parser.parse_args()

    rows = load_rows(args.trace, args.leg)
    sheet = Image.open(args.sheet).convert('RGBA')
    points = [(int(r['x']) / WORLD_PER_PIXEL, int(r['y']) / WORLD_PER_PIXEL) for r in rows]

    margin = CELL
    min_x = min(p[0] for p in points) - margin
    min_y = min(p[1] for p in points) - margin
    width = int(max(p[0] for p in points) - min_x + margin)
    height = int(max(p[1] for p in points) - min_y + margin)

    track = Image.new('RGBA', (width, height), SAND)
    draw = ImageDraw.Draw(track)
    for x in range(0, width, 16):
        draw.line([(x - min_x % 16, 0), (x - min_x % 16, height)], fill=(186, 154, 102, 255))
    for y in range(0, height, 16):
        draw.line([(0, y - min_y % 16), (width, y - min_y % 16)], fill=(186, 154, 102, 255))
    draw.line([(x - min_x, y - min_y) for x, y in points], fill=PATH, width=2)

    travelled, last = 0.0, None
    next_stamp = 0.0
    for row, (x, y) in zip(rows, points):
        if last is not None:
            travelled += math.hypot(x - last[0], y - last[1]) / 16.0
        last = (x, y)
        if travelled >= next_stamp:
            track.alpha_composite(cell(sheet, row), (int(x - min_x - CELL / 2), int(y - min_y - CELL / 2)))
            next_stamp += args.every_tiles

    sampled = rows[::args.every_cycles][:args.strip_frames]
    strip = Image.new('RGBA', (len(sampled) * CELL, CELL), SAND)
    for i, row in enumerate(sampled):
        strip.alpha_composite(cell(sheet, row), (i * CELL, 0))

    title_height = 18 if args.title else 0
    out = Image.new('RGBA', (max(track.width, strip.width), title_height + track.height + 8 + strip.height),
                    (255, 255, 255, 255))
    if args.title:
        ImageDraw.Draw(out).text((4, 3), args.title, fill=(0, 0, 0, 255))
    out.alpha_composite(track, (0, title_height))
    out.alpha_composite(strip, (0, title_height + track.height + 8))
    out.save(args.out)
    print('wrote', args.out, out.size)


if __name__ == '__main__':
    main()
