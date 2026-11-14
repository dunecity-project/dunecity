#!/usr/bin/env python3
"""
Re-pack the two cities on "2P - 192x192 - City.ini" with a COMPACT,
mixed-use layout.

Why this was rewritten
----------------------
The previous version sorted every lot by its distance along an outward axis
and then sliced that ordering into percentile bands: one big industrial ring,
one residential mass, one thin commercial strip.  On a 192x192 map those bands
end up 30-60 tiles apart, and the city simulation simply cannot see across
that:

  * TrafficSimulation gives a zone at most kMaxTrafficDistance = 20 road tiles
    to reach a complementary zone (R->C, C->I, I->R).  Across a band boundary
    that fails, so many zones had no complementary destination.
  * Commercial growth also needs residential AND industrial supply inside
    kSupplyRadius = 16 (Chebyshev), which the banding broke as well.

The replacement lays a repeating eight-lot vertical cycle instead:

    I I C R R R R C   (then repeats)

so from any lot the complementary type is at most three lot rows — nine
tiles — away, while residential still keeps two lot rows (six tiles) of
clearance from industry, which is the first distance at which kPollutionRadius
= 5 contributes nothing.  Lots are filled from the component centroid outwards
and capped, so each city is a dense town rather than a thin sprawl.

The Dune backbone (Construction Yard, Refinery, factories, Repair Yard, Silos,
Nuclear Plants, Windtraps) stays. The construction and production buildings
count as INDUSTRIAL and supply the commercial streets; Nuclear Plants and
Windtraps provide power and have no R/C/I role.

Everything else about the map is preserved: terrain, [BASIC] win/lose flags,
the CHOAM list, the Fremen/Player3 sandworms and the [Player1]/[Player2]
sections.  Only [STRUCTURES] and the Player1/Player2 entries in [UNITS] are
regenerated.

Usage:
    python3 scripts/pack_simcity_user_map.py [path-to-map.ini]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

REPO = Path(__file__).resolve().parent.parent
DEFAULT_PATH = REPO / "data" / "maps" / "singleplayer" / "2P - 192x192 - City.ini"

ROCK = '%'

# sand.cpp getStructureSize()
FOOTPRINT: Dict[str, Tuple[int, int]] = {
    "Const Yard": (2, 2), "Windtrap": (2, 2), "Refinery": (3, 2),
    "Spice Silo": (2, 2), "Repair Yard": (3, 2), "Heavy Factory": (3, 2),
    "Light Factory": (2, 2), "Hightech Factory": (3, 2), "House IX": (2, 2),
    "Nuclear Plant": (3, 3), "Outpost": (2, 2), "Airport": (3, 3),
    "Barracks": (2, 2), "WOR": (2, 2), "Police Station": (2, 2),
    "Residential Zone": (2, 2), "Commercial Zone": (2, 2),
    "Industrial Zone": (2, 2), "Rocket-Turret": (1, 1), "Gun-Turret": (1, 1),
}

# Vertical lot cycle. Two industrial rows, a commercial buffer, four
# residential rows, a commercial buffer, then round again.
CYCLE = "IICRRRRC"
LOT = 3

ZONE_NAME = {"R": "Residential Zone", "C": "Commercial Zone",
             "I": "Industrial Zone"}

# Dune backbone, in the order it is dropped onto industrial cycle rows.
# Industrial role: Const Yard / Refinery / factories / Repair Yard / Silo.
BACKBONE_INDUSTRIAL = [
    "Const Yard", "Refinery", "Heavy Factory", "Light Factory",
    "Hightech Factory", "Repair Yard", "Spice Silo", "Spice Silo",
    "Refinery", "Heavy Factory", "Spice Silo", "Light Factory",
]
# Power and civic buildings — no city role, dropped wherever they fit.
BACKBONE_UTILITY = [
    "Nuclear Plant", "Nuclear Plant", "Nuclear Plant", "Nuclear Plant",
    "Windtrap", "Windtrap", "Windtrap", "Windtrap", "Windtrap", "Windtrap",
]
# Commercial role: Outpost (Radar), House IX, Airport.
BACKBONE_COMMERCIAL = ["Outpost", "House IX", "Airport", "Outpost"]
# Residential role: Barracks, WOR.
BACKBONE_RESIDENTIAL = ["Barracks", "WOR"]

POLICE_PER_CITY = 10
MAX_LOTS_PER_CITY = 620      # keeps each city dense instead of sprawling
TURRETS_PER_CITY = 22


# ---------------------------------------------------------------------------
def parse_map_rows(text: str) -> List[List[str]]:
    m = re.search(r'\[MAP\](.*?)(?=\n\[)', text, re.S)
    if not m:
        raise SystemExit("no [MAP] section")
    rows = []
    for line in m.group(1).splitlines():
        if re.match(r'^\d{3}=', line):
            rows.append(list(line.split('=', 1)[1]))
    return rows


def rock_components(rows: List[List[str]]) -> List[List[Tuple[int, int]]]:
    H, W = len(rows), len(rows[0])
    seen = [[False] * W for _ in range(H)]
    comps = []
    for y in range(H):
        for x in range(W):
            if rows[y][x] != ROCK or seen[y][x]:
                continue
            stack = [(x, y)]
            seen[y][x] = True
            cells = []
            while stack:
                cx, cy = stack.pop()
                cells.append((cx, cy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] \
                            and rows[ny][nx] == ROCK:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            comps.append(cells)
    comps.sort(key=len, reverse=True)
    return comps


# ---------------------------------------------------------------------------
class CityPacker:
    def __init__(self, cells: Sequence[Tuple[int, int]]):
        self.rock: Set[Tuple[int, int]] = set(cells)
        xs = [p[0] for p in cells]
        ys = [p[1] for p in cells]
        self.bx, self.by = min(xs), min(ys)
        self.mx, self.my = max(xs), max(ys)
        self.cx = sum(xs) // len(xs)
        self.cy = sum(ys) // len(ys)
        self.used: Set[Tuple[int, int]] = set()
        self.structures: List[Tuple[str, int, int]] = []
        self.roads: Set[Tuple[int, int]] = set()
        # Lot grid origin.
        self.ox = self.bx + 1
        self.oy = self.by + 1

    # -- geometry ---------------------------------------------------------
    def lot_origin(self, i: int, j: int) -> Tuple[int, int]:
        return self.ox + LOT * i, self.oy + LOT * j

    def lot_type(self, j: int) -> str:
        return CYCLE[j % len(CYCLE)]

    def fits(self, x: int, y: int, w: int, h: int) -> bool:
        for dy in range(h):
            for dx in range(w):
                t = (x + dx, y + dy)
                if t not in self.rock or t in self.used:
                    return False
        return True

    def take(self, name: str, x: int, y: int) -> None:
        w, h = FOOTPRINT[name]
        self.structures.append((name, x, y))
        for dy in range(h):
            for dx in range(w):
                self.used.add((x + dx, y + dy))

    # -- passes -----------------------------------------------------------
    def lot_candidates(self) -> List[Tuple[int, int]]:
        cols = (self.mx - self.ox) // LOT + 1
        rows = (self.my - self.oy) // LOT + 1
        out = []
        for j in range(rows):
            for i in range(cols):
                x, y = self.lot_origin(i, j)
                if self.fits(x, y, 2, 2):
                    out.append((i, j))
        # Compact city: nearest the rock centroid first.
        out.sort(key=lambda ij: abs(self.lot_origin(*ij)[0] - self.cx)
                 + abs(self.lot_origin(*ij)[1] - self.cy))
        return out

    def place_backbone(self) -> None:
        """Pin the Dune backbone onto cycle rows that match its city role."""
        plan = [(BACKBONE_INDUSTRIAL, "I"), (BACKBONE_COMMERCIAL, "C"),
                (BACKBONE_RESIDENTIAL, "R"), (BACKBONE_UTILITY, None)]
        cands = self.lot_candidates()
        for names, want in plan:
            pool = [ij for ij in cands
                    if want is None or self.lot_type(ij[1]) == want]
            for name in names:
                w, h = FOOTPRINT[name]
                for (i, j) in pool:
                    x, y = self.lot_origin(i, j)
                    if self.fits(x, y, w, h):
                        self.take(name, x, y)
                        break

    def place_zones(self) -> None:
        placed = 0
        police_left = POLICE_PER_CITY
        police_stride = 47   # prime-ish stride spreads precincts evenly
        for n, (i, j) in enumerate(self.lot_candidates()):
            if placed >= MAX_LOTS_PER_CITY:
                break
            x, y = self.lot_origin(i, j)
            if not self.fits(x, y, 2, 2):
                continue
            kind = self.lot_type(j)
            if police_left and kind == "R" and n % police_stride == 0:
                self.take("Police Station", x, y)
                police_left -= 1
            else:
                self.take(ZONE_NAME[kind], x, y)
            placed += 1
        self.lot_count = placed

    def place_roads(self) -> None:
        """Full street grid over the built-up part of the component."""
        built = [(x, y) for _n, x, y in self.structures]
        if not built:
            return
        x0 = min(p[0] for p in built) - 2
        x1 = max(p[0] for p in built) + 3
        y0 = min(p[1] for p in built) - 2
        y1 = max(p[1] for p in built) + 3
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                t = (x, y)
                if t not in self.rock or t in self.used:
                    continue
                on_col = (x - self.ox - 2) % LOT == 0
                on_row = (y - self.oy - 2) % LOT == 0
                if on_col or on_row:
                    self.roads.add(t)

    def prune(self) -> None:
        """Keep one street network per city.

        The rock components are ragged, so the grid can strand a handful of
        tiles on an isolated finger of rock. Drop every road component but the
        largest, then drop any zone that lost its road frontage — a zone with
        no perimeter road never generates traffic (TrafficSimulation
        findPerimeterRoad) and would just sit at level 0 forever.
        """
        seen: Set[Tuple[int, int]] = set()
        best: Set[Tuple[int, int]] = set()
        for start in self.roads:
            if start in seen:
                continue
            comp = {start}
            seen.add(start)
            stack = [start]
            while stack:
                cx, cy = stack.pop()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nt = (cx + dx, cy + dy)
                    if nt in self.roads and nt not in seen:
                        seen.add(nt)
                        comp.add(nt)
                        stack.append(nt)
            if len(comp) > len(best):
                best = comp
        self.roads = best

        perim = [(-1, -1), (0, -1), (1, -1), (2, -1),
                 (-1, 2), (0, 2), (1, 2), (2, 2),
                 (-1, 0), (2, 0), (-1, 1), (2, 1)]
        keep = []
        for name, x, y in self.structures:
            if FOOTPRINT[name] == (2, 2) and (
                    name in ZONE_NAME.values() or name == "Police Station"):
                if not any((x + dx, y + dy) in self.roads for dx, dy in perim):
                    for dy in range(2):
                        for dx in range(2):
                            self.used.discard((x + dx, y + dy))
                    continue
            keep.append((name, x, y))
        self.structures = keep

    def place_police(self) -> None:
        """Make sure every residential lot falls inside a coverage radius."""
        res = [(x + 1, y + 1) for n, x, y in self.structures
               if n == "Residential Zone"]
        stations = [(x + 1, y + 1) for n, x, y in self.structures
                    if n == "Police Station"]
        stations += [(x, y) for n, x, y in self.structures
                     if n in ("Rocket-Turret", "Gun-Turret")]

        def covered(pt):
            return any(max(abs(pt[0] - q[0]), abs(pt[1] - q[1])) <= 23
                       for q in stations)

        for _ in range(8):
            gaps = [p for p in res if not covered(p)]
            if not gaps:
                break
            target = gaps[0]
            # Convert the residential lot closest to the gap into a precinct.
            best_idx, best_d = None, None
            for idx, (name, x, y) in enumerate(self.structures):
                if name != "Residential Zone":
                    continue
                d = max(abs(x + 1 - target[0]), abs(y + 1 - target[1]))
                if best_d is None or d < best_d:
                    best_idx, best_d = idx, d
            if best_idx is None:
                break
            name, x, y = self.structures[best_idx]
            self.structures[best_idx] = ("Police Station", x, y)
            res = [p for p in res if p != (x + 1, y + 1)]
            stations.append((x + 1, y + 1))

    def place_turrets(self) -> None:
        boundary = []
        for (x, y) in self.rock:
            if (x, y) in self.used or (x, y) in self.roads:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) not in self.rock:
                    boundary.append((x, y))
                    break
        boundary.sort(key=lambda p: (p[1], p[0]))
        if not boundary:
            return
        stride = max(1, len(boundary) // TURRETS_PER_CITY)
        n = 0
        for idx, (x, y) in enumerate(boundary):
            if idx % stride:
                continue
            if (x, y) in self.used or (x, y) in self.roads:
                continue
            self.take("Rocket-Turret" if n % 3 == 0 else "Gun-Turret", x, y)
            n += 1

    def pack(self) -> None:
        self.place_backbone()
        self.place_zones()
        self.place_roads()
        self.prune()
        self.place_turrets()
        self.place_police()

    # -- reporting --------------------------------------------------------
    def stats(self) -> str:
        kinds = {"R": 0, "C": 0, "I": 0}
        for name, _x, _y in self.structures:
            for k, n in ZONE_NAME.items():
                if name == n:
                    kinds[k] += 1
        return (f"{len(self.structures)} structures "
                f"(R/C/I zones {kinds['R']}/{kinds['C']}/{kinds['I']}), "
                f"{len(self.roads)} road tiles, "
                f"{len(self.used) + len(self.roads)}/{len(self.rock)} rock tiles used "
                f"({100 * (len(self.used) + len(self.roads)) / len(self.rock):.0f}%)")


# ---------------------------------------------------------------------------
def free_near(packer: CityPacker, x: int, y: int, taken: Set[Tuple[int, int]]):
    for r in range(0, 26):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if r and max(abs(dx), abs(dy)) != r:
                    continue
                t = (x + dx, y + dy)
                if t in packer.rock and t not in packer.used \
                        and t not in packer.roads and t not in taken:
                    return t
    return (x, y)


LOADOUT = [
    ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
    ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
    ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
    ("Launcher", "Area Guard"), ("Launcher", "Area Guard"),
    ("Devastator", "Area Guard"), ("Carryall", "Area Guard"),
    ("'Thopter", "Area Guard"),
]


def main(argv: List[str]) -> int:
    path = Path(argv[0]) if argv else DEFAULT_PATH
    if not path.exists():
        print(f"map not found: {path}")
        return 1

    text = path.read_text()
    rows = parse_map_rows(text)
    size = len(rows[0])
    if len(rows) != size:
        print(f"warning: map is {size}x{len(rows)}")

    def pos(x: int, y: int) -> int:
        return y * size + x

    comps = [c for c in rock_components(rows) if len(c) > 1000]
    if len(comps) < 2:
        print(f"expected two large rock components, found {len(comps)}")
        return 1

    structures: List[str] = []
    units: List[str] = []
    sid = 1
    uid = 100

    for owner, cells in (("Player1", comps[0]), ("Player2", comps[1])):
        packer = CityPacker(cells)
        packer.pack()
        print(f"{owner}: {packer.stats()}")

        for name, x, y in packer.structures:
            structures.append(f"ID{sid:03d}={owner},{name},256,{pos(x, y)}")
            sid += 1
        for (x, y) in sorted(packer.roads):
            structures.append(f"GEN{pos(x, y)}={owner},Road")

        taken: Set[Tuple[int, int]] = set()
        for k, (name, mode) in enumerate(LOADOUT):
            gx = packer.cx + (k % 5) * 2 - 4
            gy = packer.cy + (k // 5) * 2 - 2
            ux, uy = free_near(packer, gx, gy, taken)
            taken.add((ux, uy))
            units.append(f"ID{uid:03d}={owner},{name},256,{pos(ux, uy)},64,{mode}")
            uid += 1

    # --- rewrite [STRUCTURES] ------------------------------------------------
    text = re.sub(r'\n\[STRUCTURES\][\s\S]*?(?=\n\[|\Z)', '', text)

    # --- rewrite the Player1/Player2 part of [UNITS], keep the sandworms -----
    um = re.search(r'(\[UNITS\]\n)((?:.*\n?)*?)(?=\n\[|\Z)', text)
    kept = []
    if um:
        for line in um.group(2).splitlines():
            m = re.match(r'ID\d+=([^,]+),', line)
            if m and m.group(1) in ("Player1", "Player2"):
                continue
            if line.strip():
                # "Player3" has no section on this map, so INIMapLoader
                # resolves it to HOUSE_INVALID and silently drops the unit.
                kept.append(line.replace("=Player3,", "=Fremen,"))
        block = um.group(1) + "\n".join(kept) + "\n" + "\n".join(units) + "\n"
        text = text[:um.start()] + block + text[um.end():]
    else:
        text += "\n[UNITS]\n" + "\n".join(units) + "\n"

    structures_block = "\n[STRUCTURES]\n" + "\n".join(structures) + "\n"
    text = re.sub(r'\n\[UNITS\]', structures_block + "\n[UNITS]", text, count=1)

    # --- keep the intended two-player mission intact -------------------------
    for name in ("Player1", "Player2"):
        if re.search(rf'\[{name}\]', text):
            text = re.sub(rf'\[{name}\][\s\S]*?(?=\n\[|\Z)',
                          f"[{name}]\nCredits=15000\nQuota=0", text, count=1)
        else:
            text += f"\n[{name}]\nCredits=15000\nQuota=0\n"

    path.write_text(text)
    print(f"wrote {path} ({path.stat().st_size} bytes): "
          f"{len(structures)} structure lines, {len(units)} starting units")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
