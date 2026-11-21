#!/usr/bin/env python3
"""Build runtime atlases from the tracked Micropolis raw tiles (Pillow required).

Art: Electronic Arts, GPL-3.0+, see imported_sprites/micropolis/NOTICE.txt.
Tile IDs/sequences: MicropolisCore/src/MicropolisEngine/doc/AnimationSequences.txt,
src/zone.cpp and src/simulate.cpp. No network or simulation changes.
"""
import argparse
import importlib.util
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("micropolis_import", ROOT / "scripts/import-micropolis.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)

# Explicit corrected chimney entry points: do not animate vacant industry's
# tile 620 (an upstream animation-table bug), and replace BOTH chimney tiles.
CHIMNEYS = {621: (852, 8), 641: (884, 4), 644: (888, 4),
            649: (892, 4), 650: (896, 4), 676: (900, 4),
            677: (904, 4), 686: (908, 4), 689: (912, 4)}
ROAD_MASKS = [66, 67, 66, 68, 67, 67, 69, 73,
              66, 71, 66, 72, 70, 75, 74, 76]
# Original traffic loop order is 80 -> 128 -> 112 -> 96 -> 80.
TRAFFIC_PHASES = [0, 3, 2, 1]


def composite(tiles, base, size, replacements=None, target=32):
    replacements = replacements or {}
    full = Image.new("RGBA", (size * 16, size * 16))
    for n in range(size * size):
        tile = base + n
        full.paste(tiles[replacements.get(tile, tile)], ((n % size) * 16, (n // size) * 16))
    return importer.downscale_pixel_art(full, target)


def build(raw_dir):
    tiles = [Image.open(raw_dir / f"tile_{n:03d}.png").convert("RGBA") for n in range(960)]
    atlases = {}
    manifest = {"source": "Micropolis; GPL-3.0+; see ../NOTICE.txt", "atlases": {}}

    def save(name, cols, rows, cell, frames, models):
        atlas = Image.new("RGBA", (cols * cell, rows * cell))
        for col, row, img in frames:
            atlas.paste(img, (col * cell, row * cell))
        atlases[name] = atlas
        manifest["atlases"][name] = {"columns": cols, "rows": rows, "cell": cell, "models": models}

    # All 16 apartment variants, plus all 12 single-house tiles. Houses are
    # arranged on the eight perimeter sites of the original free residential lot.
    frames = []
    for value in range(4):
        frames.append((0, value, composite(tiles, 240, 3)))
        for house in range(3):
            # Stable perimeter order: growth adds one house, decline removes
            # one, preserving every house already visible on the lot.
            perimeter = [0, 1, 2, 5, 8, 7, 6, 3]
            for count in range(1, 9):
                repl = {240 + n: 249 + value * 3 + house for n in perimeter[:count]}
                frames.append((1 + house * 8 + count - 1, value, composite(tiles, 240, 3, repl)))
        for density in range(4):
            frames.append((25 + density, value, composite(tiles, 261 + (value * 4 + density) * 9, 3)))
    frames = [(col % 15, value * 2 + col // 15, img) for col, value, img in frames]
    save("residential", 15, 8, 32, frames, 112)

    frames = []
    for value in range(4):
        frames.append((0, value, composite(tiles, 423, 3)))
        for density in range(5):
            frames.append((1 + density, value, composite(tiles, 432 + (value * 5 + density) * 9, 3)))
    save("commercial", 6, 4, 32, frames, 20)

    # Rows: value tier * 9 + phase (0 = unpowered/static, 1..8 = powered).
    # Keep phases vertical so all zoom levels fit even a 2048px texture limit.
    frames = []
    for value in range(2):
        for phase in range(9):
            row = value * 9 + phase
            frames.append((0, row, composite(tiles, 612, 3)))
            repl = {} if phase == 0 else {tile: start + (phase - 1) % count
                                         for tile, (start, count) in CHIMNEYS.items()}
            for density in range(4):
                frames.append((1 + density, row, composite(tiles, 621 + (value * 4 + density) * 9, 3, repl)))
    save("industrial", 5, 18, 32, frames, 8)

    for name, base, size in [("airport", 709, 6), ("stadium", 779, 4), ("nuclear", 811, 4)]:
        frames = [(0, 0, composite(tiles, base, size, target=48))]
        for phase in range(8):
            active_base = base
            if name == "airport":
                repl = {711: 832 + phase}
            elif name == "stadium":
                # FULLSTADIUM=800 is the centre; its first tile is 800-5=795.
                active_base = 795
                repl = {801: 932 + phase, 805: 940 + phase}
            else:
                repl = {820: 952 + phase % 4}
            frames.append((phase + 1, 0, composite(tiles, active_base, size, repl, target=48)))
        save(name, 9, 1, 48, frames, 1)

    # Park (Micropolis tool.cpp putDownPark): WOODS2..WOODS5 (tiles 40-43), then the
    # animated FOUNTAIN (840-843). One 16-pixel tile per frame, the 1x1 footprint.
    frames = [(n, 0, tiles[tile]) for n, tile in enumerate([40, 41, 42, 43, 840, 841, 842, 843])]
    save("park", 8, 1, 16, frames, 5)

    # Retain DuneCity's asphalt curbs, but preserve original vehicle pixels and
    # markings. The old recolour erased black cars and stamped a dot over them.
    frames = []
    for row in range(9):
        for mask, road in enumerate(ROAD_MASKS):
            if row == 0:
                tile = road
            else:
                density = 80 if row <= 4 else 144
                tile = density + (road - 64) + 16 * TRAFFIC_PHASES[(row - 1) % 4]
            img = tiles[tile].copy()
            pixels = [img.getpixel((x, y)) for y in range(img.height) for x in range(img.width)]
            img.putdata([(90, 90, 90, a) if (r, g, b) in [(204, 127, 102), (0, 230, 0)]
                         else (r, g, b, a) for r, g, b, a in pixels])
            frames.append((mask, row, img))
    save("roads", 16, 9, 16, frames, 16)
    return atlases, manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "imported_sprites/micropolis")
    parser.add_argument("--check", action="store_true", help="Verify committed pixels/manifest are reproducible")
    args = parser.parse_args()
    atlases, manifest = build(args.root / "raw_tiles")
    output = args.root / "atlases"
    if not args.check:
        output.mkdir(parents=True, exist_ok=True)
    for name, atlas in atlases.items():
        path = output / (name + ".png")
        if args.check:
            with Image.open(path) as existing:
                assert existing.size == atlas.size and existing.convert("RGBA").tobytes() == atlas.tobytes(), path
        else:
            atlas.save(path)
    path = output / "manifest.json"
    if args.check:
        assert json.loads(path.read_text()) == manifest
    else:
        path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{'Verified' if args.check else 'Generated'} {len(atlases)} city atlases")


if __name__ == "__main__":
    main()
