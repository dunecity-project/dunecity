# Runtime city animations and model inventory (1.0.619)

The runtime now loads **tracked, prebuilt atlases** from
`imported_sprites/micropolis/atlases/`. Pillow is an authoring dependency only;
normal builds and releases copy these PNGs without running Python. The older
individual composites below remain useful for inspection and civic icons.

```sh
python3 scripts/build-city-atlases.py          # regenerate from tracked raw tiles
python3 scripts/build-city-atlases.py --check  # compare every pixel and manifest
```

A full `import-micropolis.py` run also invokes the atlas builder. It reads the
original tile IDs and assembles the full source footprint before downscaling:
2x2 R/C/I, 4x4 airport, 3x3 stadium/nuclear, 1x1 road. Original art attribution is in
`imported_sprites/micropolis/NOTICE.txt`; no simulation footprint changed.

The airport cell is 64 px since 2026-11-23, when the Airport's footprint grew from 3x3 to 4x4
(the original sprite is 6x6 tiles, 96 px). An Airport that has to stay 3x3 (an older save, or a
map with no room for the fourth row and column) draws the same cell scaled into three tiles.

| Atlas | Columns x rows | Content |
|---|---:|---|
| residential | 8 x 4 | vacant + three house styles + four apartments, per value tier |
| commercial | 6 x 4 | vacant + all five commercial sizes, per value tier |
| industrial | 5 x 18 | vacant + four factories; two value tiers, each static + eight phases |
| roads | 16 x 9 | connection masks; static + four light + four heavy traffic phases |
| airport | 9 x 1 | static + eight rotating radar phases |
| stadium | 9 x 1 | empty + eight full-stadium football phases |
| nuclear | 9 x 1 | static + eight phases (four-frame swirl repeated) |

All 16 apartment models, 12 single-house styles, 20 commercial models and eight
industrial models are reachable. The original free residential lot fills eight
perimeter sites with houses. Stable coordinate hashing chooses visual variants
within our three inhabited density levels; no gameplay RNG or save fields are
added. Animated frames do not change the chosen building model.

Source references: [Micropolis animation sequences](https://github.com/SimHacker/micropolis/blob/master/MicropolisCore/src/MicropolisEngine/doc/AnimationSequences.txt),
`simulate.cpp::doRoad/doSpecialZone/drawStadium` and `zone.cpp::setSmoke`.
`FULLSTADIUM=800` is the centre; its 4x4 graphic starts at795, not800.
Smoke uses corrected tile621 rather than empty industry's tile620, and both
chimney positions are animated (upstream setSmoke overwrites the same position
twice; we use the documented IND2..IND9 sequence positions). Airport radar is
711->832..839, football panels932..939/940..947, nuclear swirl820->952..955.

Traffic uses the original **0..63 none,64..191 light,192..255 heavy** display
thresholds. Vehicle pixels stay intact: the former broad recolour/centre-dot
stamp erased black cars. Only grass/peach curb pixels become Dune asphalt.
Hidden roads display no live traffic. Power gates factory smoke and radar;
stadiums show eight seconds of matches in each32-second visual cycle, offset
by site. Animation phases advance every128 simulation milliseconds, so pause
freezes them. This cadence is a visual adaptation, not a calendar/simulation port.

`CitySpritePolicy.h` and GFXManager's layout metadata must agree with the atlas
builder. Seven atlases share their house-independent surfaces/textures instead
of being cloned18times. All zoomed dimensions stay <=2048. Tests decode the
shipped PNGs, check reachable models, frame bounds, real frame differences,
nonanimated vacant factories, traffic thresholds, power gating and match cycles.

---

# Sprite Import Scripts

Scripts to extract building sprites from a local copy of the original Dune II
game files for use as DuneCity zone art.

**These scripts do NOT bundle copyrighted assets into the repo.** Output goes to
a local directory that is gitignored.

## Prerequisites

- Python 3.8+
- Pillow: `pip install Pillow`
- A local copy of the Dune II DATA directory (containing ICON.ICN, ICON.MAP,
  IBM.PAL, and .WSA files)

## import-sprites.py — Structure tile sprites (ICN/MAP)

Extracts composed structure graphics from the ICN tileset:

```bash
# Export all known structures as 2× PNGs:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA

# Export specific MAP entries at 1× scale:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA --entries 17,19,21 --scale 1

# Custom output directory:
python3 scripts/import-sprites.py /path/to/DUNE2/DATA --out ./my_sprites
```

Output: `./imported_sprites/<structure_name>.png`

## extract-wsa.py — Animation frames (WSA)

Extracts individual frames from WSA animation files (build-menu portraits):

```bash
# Extract all WSA files:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA

# Extract specific files:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA --files WINDTRAP.WSA,CONSTRUC.WSA

# 2× upscale:
python3 scripts/extract-wsa.py /path/to/DUNE2/DATA --scale 2
```

Output: `./imported_sprites/wsa/<name>_frameNN.png`

## Importing into DuneCity

Once extracted, the PNGs can be placed in `data/` subdirectories and loaded by
GFXManager. The current zone placeholders in `GFXManager.cpp` are colored
rectangles — replace the `makeZonePlaceholder()` calls with proper PNG loading
once you have art ready.

The `imported_sprites/` directory is gitignored to prevent accidental commits.

## import-micropolis.py — Micropolis (open-source city sim) tiles

Extracts all 960 16×16 tiles from the Micropolis tile sheet (`tiles.xpm`),
plus categorised subsets and assembled multi-tile composites for buildings
and zones.

### Prerequisites

- Python 3.8+
- Pillow: `pip install Pillow`
- (Optional) a local Micropolis source checkout

### Usage

```bash
# Auto-download Micropolis source (cloned into a temp cache):
python3 scripts/import-micropolis.py

# Use a local Micropolis checkout:
python3 scripts/import-micropolis.py --micropolis-dir /path/to/micropolis

# Custom output directory:
python3 scripts/import-micropolis.py --out ./my_output
```

### Output structure

```
imported_sprites/micropolis/
├── raw_tiles/           # tile_000.png .. tile_959.png (all 960 tiles)
├── categories/          # Tiles grouped by function
│   ├── roads/           # Road/bridge/intersection tiles
│   ├── power_lines/     # Power line tiles
│   ├── rail/            # Railroad tiles
│   ├── residential_zones/  # Residential building tiles
│   ├── commercial_zones/   # Commercial building tiles
│   ├── industrial_zones/   # Industrial building tiles
│   ├── airport/         # Airport tiles
│   ├── seaport/         # Seaport tiles
│   ├── coal_power/      # Coal power plant tiles
│   ├── nuclear_power/   # Nuclear power plant tiles
│   └── ...              # 36 categories total
├── composites/          # Assembled 3×3 building PNGs (48×48)
│   ├── res_v0_d0_3x3.png   # Residential value 0, density 0
│   ├── com_v1_d2_3x3.png   # Commercial value 1, density 2
│   └── ...              # 49 zone/building composites
├── composites_2x2/      # 2×2 adapted versions (32×32) for DuneCity
├── composites_special/  # Large buildings: 4×4 and 6×6
│   ├── coal_power_plant_4x4.png
│   ├── nuclear_power_plant_4x4.png
│   ├── airport_6x6.png
│   └── ...
├── composites_special_2x2/  # Large buildings downscaled to 2×2
├── manifest.json        # Full index of all exported files
└── NOTICE.txt           # Licensing and attribution
```

### Naming conventions

- **Residential**: `res_v{value}_d{density}` — value 0–3 (land value), density 0–3
- **Commercial**: `com_v{value}_d{density}` — value 0–3, density 0–4
- **Industrial**: `ind_v{value}_d{density}` — value 0–1, density 0–3
- **Special**: `hospital`, `church`, `fire_station`, `police_station`, `seaport`
- **Large**: `coal_power_plant`, `nuclear_power_plant`, `stadium`, `airport`

### License / trademark caveats

The Micropolis tile art is Copyright (C) 1989–2007 Electronic Arts Inc.,
released under GPL-3.0+.  The additional terms prohibit use of the
"SimCity" trademark.  See `NOTICE.txt` in the output directory.  Do **not**
use "SimCity" in filenames, docs, or any distributed materials.
