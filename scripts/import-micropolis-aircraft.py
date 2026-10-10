#!/usr/bin/env python3
"""Import the original Micropolis helicopter and airplane sprites.

The artwork is the unmodified X11 pixel art shipped with the GPL Micropolis
release: ``images/obj2-0.xpm`` .. ``obj2-7.xpm`` (32x32 traffic helicopter, the
eight compass headings) and ``images/obj3-0.xpm`` .. ``obj3-10.xpm`` (48x48
airplane, the same eight headings plus the three take-off frames).  Nothing is
redrawn, recoloured or rescaled: every output pixel is a source pixel.

Micropolis numbers sprite frames from 1 (frame 0 means "sprite inactive"), so
``objN-k.xpm`` holds sprite frame ``k+1``.  ``sprite.cpp``'s direction tables

    CDx[9] = { 0,  0,  3,  5,  3,  0, -3, -5, -3 }   (helicopter)
    CDy[9] = { 0, -5, -3,  0,  3,  5,  3,  0, -3 }

give frame 1 = north, 2 = north-east, 3 = east ... 8 = north-west, which is how
the columns below are re-ordered into Dune Legacy's ANGLETYPE order
(RIGHT, RIGHTUP, UP, LEFTUP, LEFT, LEFTDOWN, DOWN, RIGHTDOWN).

Output (tracked, and copied into the app bundle by src/CMakeLists.txt):

    imported_sprites/micropolis/aircraft/city_helicopter.png   8 x 1 cells of 32px
    imported_sprites/micropolis/aircraft/city_airplane.png     8 x 4 cells of 48px
    imported_sprites/micropolis/aircraft/manifest.json

The airplane rows are: row 0 the eight cruise headings, rows 1..3 the take-off
frames 9, 10 and 11.  Take-off art is heading-independent in the original, so
each take-off row repeats its single frame across all eight columns; that keeps
the renderer's angle indexing harmless without inventing any new artwork.

Usage:
    python3 scripts/import-micropolis-aircraft.py                # regenerate
    python3 scripts/import-micropolis-aircraft.py --check        # verify tracked PNGs
    python3 scripts/import-micropolis-aircraft.py --source DIR   # other reference checkout

Pillow is an authoring dependency only, exactly as for build-city-atlases.py:
normal builds and releases just copy the tracked PNGs.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = Path.home() / "Documents/projects/simcity/micropolis/micropolis-activity/images"
OUT_DIR = ROOT / "imported_sprites/micropolis/aircraft"

# Dune Legacy ANGLETYPE order -> Micropolis sprite frame number (1..8).
# RIGHT, RIGHTUP, UP, LEFTUP, LEFT, LEFTDOWN, DOWN, RIGHTDOWN
ANGLE_TO_MICROPOLIS_FRAME = [3, 2, 1, 8, 7, 6, 5, 4]
ANGLE_NAMES = ["RIGHT", "RIGHTUP", "UP", "LEFTUP", "LEFT", "LEFTDOWN", "DOWN", "RIGHTDOWN"]

# sprite.cpp doAirplaneSprite(): frames 11 -> 10 -> 9 -> 3 is the take-off run.
AIRPLANE_TAKEOFF_FRAMES = [9, 10, 11]


def parse_xpm(path):
    """Decode a 1-char-per-pixel XPM into (width, height, RGBA bytes).

    Micropolis stores colours as #RRRRGGGGBBBB (16 bits per channel); the high
    byte is the 8-bit value.  "None" is the transparent key colour.
    """
    strings = []
    for line in path.read_text(encoding="latin-1").splitlines():
        line = line.strip()
        start = line.find('"')
        if start < 0:
            continue
        end = line.rfind('"')
        if end <= start:
            continue
        strings.append(line[start + 1:end])

    if not strings:
        raise ValueError("%s: no XPM string data" % path)

    header = strings[0].split()
    width, height, ncolors, cpp = (int(value) for value in header[:4])
    if cpp != 1:
        raise ValueError("%s: only 1 char per pixel is supported (got %d)" % (path, cpp))
    if len(strings) < 1 + ncolors + height:
        raise ValueError("%s: truncated XPM" % path)

    palette = {}
    for entry in strings[1:1 + ncolors]:
        key = entry[0]
        tokens = entry[1:].split()
        if "c" not in tokens:
            raise ValueError("%s: colour %r has no 'c' visual" % (path, key))
        spec = tokens[tokens.index("c") + 1]
        if spec.lower() == "none":
            palette[key] = (0, 0, 0, 0)
        elif spec.startswith("#"):
            digits = spec[1:]
            if len(digits) == 12:          # #RRRRGGGGBBBB
                channels = [int(digits[i:i + 4], 16) >> 8 for i in (0, 4, 8)]
            elif len(digits) == 6:         # #RRGGBB
                channels = [int(digits[i:i + 2], 16) for i in (0, 2, 4)]
            else:
                raise ValueError("%s: unsupported colour %r" % (path, spec))
            palette[key] = (channels[0], channels[1], channels[2], 255)
        else:
            raise ValueError("%s: unsupported colour %r" % (path, spec))

    pixels = bytearray()
    for row in strings[1 + ncolors:1 + ncolors + height]:
        if len(row) < width:
            raise ValueError("%s: short pixel row" % path)
        for key in row[:width]:
            pixels += bytes(palette[key])
    return width, height, bytes(pixels)


def frame_digest(rgba):
    return hashlib.sha256(rgba).hexdigest()


def build_sheet(frames, cell, columns, rows):
    """Compose an RGBA sheet; frames is a {(col,row): rgba bytes} mapping."""
    from PIL import Image

    sheet = Image.new("RGBA", (columns * cell, rows * cell), (0, 0, 0, 0))
    for (col, row), rgba in frames.items():
        tile = Image.frombytes("RGBA", (cell, cell), rgba)
        sheet.paste(tile, (col * cell, row * cell))
    return sheet


def sheet_bytes(sheet):
    import io

    buffer = io.BytesIO()
    # optimize keeps the tracked PNGs small and the encoder deterministic.
    sheet.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def load_source_frames(source, prefix, count, cell):
    frames = {}
    for index in range(count):
        path = source / ("%s-%d.xpm" % (prefix, index))
        if not path.is_file():
            raise SystemExit("Missing original sprite %s" % path)
        width, height, rgba = parse_xpm(path)
        if (width, height) != (cell, cell):
            raise SystemExit("%s is %dx%d, expected %dx%d" % (path, width, height, cell, cell))
        frames[index + 1] = (path, rgba)   # Micropolis frame numbers start at 1
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="Micropolis micropolis-activity/images directory (read-only)")
    parser.add_argument("--out", type=Path, default=OUT_DIR)
    parser.add_argument("--check", action="store_true",
                        help="regenerate in memory and compare against the tracked files")
    args = parser.parse_args()

    source = args.source.resolve()
    if not source.is_dir():
        raise SystemExit("Original Micropolis image directory not found: %s" % source)

    copter = load_source_frames(source, "obj2", 8, 32)
    plane = load_source_frames(source, "obj3", 11, 48)

    manifest = {
        "notice": "Original Micropolis sprite art, GPL-3.0-or-later. See ../NOTICE.txt.",
        "generator": "scripts/import-micropolis-aircraft.py",
        "source_directory": "micropolis-activity/images",
        "source_repository": "https://github.com/SimHacker/micropolis",
        "source_revision": "ccc346165564a2ceec86a588861d7e7c27911714",
        "sheets": {},
    }

    outputs = {}

    # ---- helicopter: 8 heading columns, single row -------------------------
    copter_cells = {}
    copter_frames = []
    for angle, micro in enumerate(ANGLE_TO_MICROPOLIS_FRAME):
        path, rgba = copter[micro]
        copter_cells[(angle, 0)] = rgba
        copter_frames.append({"column": angle, "row": 0, "angle": ANGLE_NAMES[angle],
                              "micropolis_frame": micro, "source": path.name,
                              "sha256": frame_digest(rgba)})
    outputs["city_helicopter.png"] = sheet_bytes(build_sheet(copter_cells, 32, 8, 1))
    manifest["sheets"]["city_helicopter.png"] = {
        "cell": 32, "columns": 8, "rows": 1,
        "rows_meaning": ["cruise headings"],
        "frames": copter_frames,
    }

    # ---- airplane: 8 heading columns, cruise row + three take-off rows -----
    plane_cells = {}
    plane_frames = []
    for angle, micro in enumerate(ANGLE_TO_MICROPOLIS_FRAME):
        path, rgba = plane[micro]
        plane_cells[(angle, 0)] = rgba
        plane_frames.append({"column": angle, "row": 0, "angle": ANGLE_NAMES[angle],
                             "micropolis_frame": micro, "source": path.name,
                             "sha256": frame_digest(rgba)})
    for row, micro in enumerate(AIRPLANE_TAKEOFF_FRAMES, start=1):
        path, rgba = plane[micro]
        for angle in range(8):
            plane_cells[(angle, row)] = rgba
        plane_frames.append({"column": None, "row": row, "angle": "takeoff",
                             "micropolis_frame": micro, "source": path.name,
                             "sha256": frame_digest(rgba)})
    outputs["city_airplane.png"] = sheet_bytes(build_sheet(plane_cells, 48, 8, 4))
    manifest["sheets"]["city_airplane.png"] = {
        "cell": 48, "columns": 8, "rows": 4,
        "rows_meaning": ["cruise headings", "take-off frame 9",
                         "take-off frame 10", "take-off frame 11"],
        "frames": plane_frames,
    }

    for name, data in outputs.items():
        manifest["sheets"][name]["sha256"] = hashlib.sha256(data).hexdigest()

    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"

    out = args.out.resolve()
    if args.check:
        failures = []
        for name, data in outputs.items():
            path = out / name
            if not path.is_file():
                failures.append("missing %s" % path)
            elif path.read_bytes() != data:
                failures.append("%s differs from the original artwork" % path)
        manifest_path = out / "manifest.json"
        if not manifest_path.is_file():
            failures.append("missing %s" % manifest_path)
        elif manifest_path.read_text() != manifest_text:
            failures.append("%s differs" % manifest_path)
        if failures:
            for failure in failures:
                print("FAIL:", failure, file=sys.stderr)
            return 1
        print("Micropolis aircraft sprites match the original artwork.")
        return 0

    out.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (out / name).write_bytes(data)
        print("wrote", out / name, len(data), "bytes")
    (out / "manifest.json").write_text(manifest_text)
    print("wrote", out / "manifest.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
