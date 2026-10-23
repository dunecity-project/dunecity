#!/usr/bin/env python3
"""Deterministic generator for the ten DuneCity single-player city scenarios.

All ten missions are stand-alone Atreides-human maps written into
``data/maps/singleplayer/``.  Three filenames are published identities and are
regenerated in place; the other seven are new:

  2P - 128x128 - Sihaya Basin.ini       cramped shelf, colonise separated rock
  3P - 128x128 - Ash Quarter.ini        neglected slum, gang outbreak pressure
  3P - 160x96  - Coriolis Gap.ini       assault an entrenched Sardaukar city
  3P - 96x96   - Cielago Watch.ini      30-minute holdout on a mesa
  3P - 128x128 - Hagal Flats.ini        spice quota with storage expansion
  3P - 160x96  - Tuono Crossing.ini     mobile expedition, no construction yard
  4P - 128x128 - Carthag Vise.ini       two-front siege of a valley city
  3P - 128x128 - Arrakeen Blackout.ini  blacked-out city, power/urban recovery
  4P - 144x112 - Shield Wall Rift.ini   three rival houses at war with each other
  3P - 160x128 - Harg Pass Convoy.ini   five scattered supply towns under raid

Design rules, all read out of the engine rather than guessed:

  * Zones are 2x2 (CLAUDE.md; sand.cpp getStructureSize).  Production
    structures use their real footprints from the same table.
  * Roads are a tile flag (``GEN<pos>=<House>,Road``) and only stick to
    rock or slab (CityConstants.h isCityBuildableTerrain).
  * A zone needs a road on its perimeter and then a road-BFS of at most
    kMaxTrafficDistance = 20 to a complementary role (R->C, C->I, I->R).
    Commercial growth also reads R/I supply inside kSupplyRadius = 16.
  * Ordinary Dune buildings carry city roles (CityEffects.h
    getStructureCityRole): Const Yard / Refinery / Silo / Light, Heavy and
    HighTech Factory / Repair Yard / Starport are INDUSTRIAL; Outpost,
    House IX and Airport are COMMERCIAL; Barracks, WOR and Palace are
    RESIDENTIAL.  Silo, Const Yard and Starport are non-polluting.
  * Crime is real policy, not decoration.  computeCrimeAfterPolice is
    ``128 - landValue + populationDensity - policeCoverage``; land value is
    ``(34 - clusterDistance/2)*4 + sandTerrainBonus - pollution``, and
    cityCrimeUnrestRate needs a displayed population of 5000 (250 internal).
    The Ash Quarter slum is therefore built dense, rock-locked (no sand
    land-value bonus), ringed by heavy industry, far from its own city
    centroid, next to hostile structures (hostileLandValuePenalty) and with
    no police station in reach.  Other maps keep police where the fiction
    wants order.
  * WinFlags bit 16 has no meaning (there is no population victory) and is
    never set here.

Cities are laid out road-first: an organic street skeleton is painted, then
2x2 lots are hung off the street frontage by district profile.  The result is
deliberately imperfect -- vacant lots, under-served pockets, dead-end spurs --
which is the point.  Assertions below only guarantee legality (in bounds, on
legal terrain, no overlaps, units placeable), never tidiness.

Run:  python3 scripts/gen_city_scenarios.py
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "data" / "maps" / "singleplayer"

# ---------------------------------------------------------------------------
# Terrain characters -- INIMapLoader.cpp:393-467
# ---------------------------------------------------------------------------
SAND = '-'
DUNES = '^'
SPICE = '~'
THICK = '+'
ROCK = '%'
MOUNTAIN = '@'
BLOOM = 'O'

DRIVEABLE = (ROCK, SAND, DUNES, SPICE, THICK)

# ---------------------------------------------------------------------------
# Footprints -- sand.cpp getStructureSize()
# ---------------------------------------------------------------------------
FOOTPRINT: Dict[str, Tuple[int, int]] = {
    "Const Yard": (2, 2),
    "Windtrap": (2, 2),
    "Refinery": (3, 2),
    "Spice Silo": (2, 2),
    "Repair Yard": (3, 2),
    "Heavy Factory": (3, 2),
    "Light Factory": (2, 2),
    "Hightech Factory": (3, 2),
    "House IX": (2, 2),
    "Palace": (3, 3),
    "Starport": (3, 3),
    "Nuclear Plant": (3, 3),
    "Stadium": (3, 3),
    "Airport": (3, 3),
    "Outpost": (2, 2),
    "Barracks": (2, 2),
    "WOR": (2, 2),
    "Police Station": (2, 2),
    "Residential Zone": (2, 2),
    "Commercial Zone": (2, 2),
    "Industrial Zone": (2, 2),
    "Rocket-Turret": (1, 1),
    "Gun-Turret": (1, 1),
    "Wall": (1, 1),
}

ZONE_NAME = {
    'R': "Residential Zone",
    'C': "Commercial Zone",
    'I': "Industrial Zone",
    'P': "Police Station",
}

# Units that exist for every house (ObjectData.ini.default has no per-house
# Enabled override), so map-placed units never get dropped silently.
UNIT_NAMES = {
    "Carryall", "Devastator", "Deviator", "Harvester", "Launcher", "MCV",
    "'Thopter", "Quad", "Raider Trike", "Siege Tank", "Sonic Tank", "Tank",
    "Trike", "Trooper", "Troopers", "Soldier", "Infantry", "Saboteur",
}

HOUSES = {"Atreides", "Harkonnen", "Ordos", "Fremen", "Sardaukar", "Mercenary"}
TEAM_BEHAVIOURS = {"Normal", "Guard", "Kamikaze", "Staging", "Flee"}
TEAM_TYPES = {"Foot", "Wheeled", "Tracked", "Winged"}
DROP_LOCATIONS = {"North", "East", "South", "West", "Air", "Visible",
                  "Enemybase", "Homebase"}
ATTACK_MODES = {"Guard", "Area Guard", "Ambush", "Hunt", "Harvest", "Stop",
                "Capture", "Retreat"}


# ---------------------------------------------------------------------------
# Deterministic integer RNG (no stdlib randomness, byte-stable everywhere)
# ---------------------------------------------------------------------------
class Rng:
    def __init__(self, seed: int):
        self.s = seed & 0xFFFFFFFF

    def next(self) -> int:
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s >> 8

    def rand(self, n: int) -> int:
        return self.next() % max(1, n)

    def between(self, a: int, b: int) -> int:
        return a + self.rand(b - a + 1)

    def chance(self, pct: int) -> bool:
        return self.rand(100) < pct

    def pick(self, seq: Sequence):
        return seq[self.rand(len(seq))]


# ---------------------------------------------------------------------------
# Terrain canvas
# ---------------------------------------------------------------------------
class Canvas:
    def __init__(self, w: int, h: int, fill: str = SAND):
        self.w = w
        self.h = h
        self.g = [[fill] * w for _ in range(h)]

    def inside(self, x: int, y: int) -> bool:
        return 0 <= x < self.w and 0 <= y < self.h

    def set(self, x: int, y: int, ch: str) -> None:
        if self.inside(x, y):
            self.g[y][x] = ch

    def get(self, x: int, y: int) -> str:
        return self.g[y][x] if self.inside(x, y) else MOUNTAIN

    def rect(self, x: int, y: int, w: int, h: int, ch: str,
             only_if: Optional[Sequence[str]] = None) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if not self.inside(xx, yy):
                    continue
                if only_if is not None and self.g[yy][xx] not in only_if:
                    continue
                self.g[yy][xx] = ch

    def disc(self, cx: int, cy: int, r: int, ch: str,
             only_if: Optional[Sequence[str]] = None) -> None:
        rr = r * r
        for yy in range(cy - r, cy + r + 1):
            for xx in range(cx - r, cx + r + 1):
                if not self.inside(xx, yy):
                    continue
                if (xx - cx) ** 2 + (yy - cy) ** 2 > rr:
                    continue
                if only_if is not None and self.g[yy][xx] not in only_if:
                    continue
                self.g[yy][xx] = ch

    def organic(self, cx: int, cy: int, r: int, ch: str, rng: Rng,
                lobes: int = 7, only_if: Optional[Sequence[str]] = None) -> None:
        """Union of jittered discs -- an irregular island, never a circle."""
        self.disc(cx, cy, max(2, r * 2 // 3), ch, only_if)
        for _ in range(lobes):
            ox = rng.between(-r, r)
            oy = rng.between(-r, r)
            if ox * ox + oy * oy > r * r:
                ox //= 2
                oy //= 2
            self.disc(cx + ox, cy + oy, rng.between(max(2, r // 3), max(3, r * 2 // 3)),
                      ch, only_if)

    def ribbon(self, pts: Sequence[Tuple[int, int]], ch: str, width: int,
               rng: Rng, only_if: Optional[Sequence[str]] = None) -> None:
        """Jittered thick polyline: ridges, canyon floors, land bridges."""
        for i in range(len(pts) - 1):
            ax, ay = pts[i]
            bx, by = pts[i + 1]
            steps = max(abs(bx - ax), abs(by - ay))
            for s in range(steps + 1):
                x = ax + (bx - ax) * s // max(1, steps)
                y = ay + (by - ay) * s // max(1, steps)
                w = width + rng.between(-1, 1)
                self.disc(x + rng.between(-1, 1), y + rng.between(-1, 1),
                          max(1, w // 2), ch, only_if)

    def spice_field(self, cx: int, cy: int, r: int, rng: Rng, rich: bool = False) -> None:
        self.organic(cx, cy, r, SPICE, rng, lobes=5, only_if=(SAND, DUNES))
        if rich:
            self.organic(cx, cy, max(2, r // 2), THICK, rng, lobes=4, only_if=(SPICE,))

    def rows(self) -> List[str]:
        return ["".join(r) for r in self.g]


# ---------------------------------------------------------------------------
# Scenario object model
# ---------------------------------------------------------------------------
@dataclass
class Placed:
    owner: str
    name: str
    x: int
    y: int
    health: int = 256


@dataclass
class Unit:
    owner: str
    name: str
    x: int
    y: int
    angle: int = 64
    mode: str = "Area Guard"
    health: int = 256


@dataclass
class Scenario:
    filename: str
    size_x: int
    size_y: int
    header: List[str]
    basic: Dict[str, object]
    houses: List[Tuple[str, Dict[str, object]]]
    canvas: Canvas
    structures: List[Placed] = field(default_factory=list)
    roads: List[Tuple[str, int, int]] = field(default_factory=list)
    units: List[Unit] = field(default_factory=list)
    reinforcements: List[str] = field(default_factory=list)
    teams: List[str] = field(default_factory=list)
    choam: Dict[str, int] = field(default_factory=dict)
    _blocked: Set[Tuple[int, int]] = field(default_factory=set)
    _roadset: Set[Tuple[int, int]] = field(default_factory=set)
    _unittiles: Set[Tuple[int, int]] = field(default_factory=set)

    # -- placement helpers -------------------------------------------------
    def fits(self, name: str, x: int, y: int,
             terrain: Sequence[str] = (ROCK,)) -> bool:
        w, h = FOOTPRINT[name]
        for dy in range(h):
            for dx in range(w):
                tx, ty = x + dx, y + dy
                if not self.canvas.inside(tx, ty):
                    return False
                if (tx, ty) in self._blocked or (tx, ty) in self._roadset:
                    return False
                if (tx, ty) in self._unittiles:
                    return False
                if self.canvas.get(tx, ty) not in terrain:
                    return False
        return True

    def place(self, owner: str, name: str, x: int, y: int,
              health: int = 256) -> Placed:
        w, h = FOOTPRINT[name]
        p = Placed(owner, name, x, y, health)
        self.structures.append(p)
        for dy in range(h):
            for dx in range(w):
                self._blocked.add((x + dx, y + dy))
        return p

    def try_place(self, owner: str, name: str, x: int, y: int,
                  health: int = 256, terrain: Sequence[str] = (ROCK,)) -> bool:
        if not self.fits(name, x, y, terrain):
            return False
        self.place(owner, name, x, y, health)
        return True

    def place_near(self, owner: str, name: str, x: int, y: int,
                   health: int = 256, radius: int = 14,
                   terrain: Sequence[str] = (ROCK,),
                   need_road: bool = True) -> Placed:
        """Nearest legal footprint to (x,y); prefers a road-adjacent lot.

        Raises when nothing legal exists -- a generator assertion, not a
        cosmetic check: a structure that cannot be placed would be silently
        dropped by the INI loader and quietly unbalance the mission.
        """
        best: Optional[Tuple[int, int]] = None
        for want_road in ((True, False) if need_road else (False,)):
            for r in range(radius + 1):
                ring = self._ring(x, y, r)
                for (tx, ty) in ring:
                    if not self.fits(name, tx, ty, terrain):
                        continue
                    if want_road and not self._touches_road(name, tx, ty):
                        continue
                    best = (tx, ty)
                    break
                if best:
                    break
            if best:
                break
        if best is None:
            raise ValueError(
                f"{self.filename}: no legal spot for {owner} {name} near ({x},{y})")
        return self.place(owner, name, best[0], best[1], health)

    @staticmethod
    def _ring(x: int, y: int, r: int) -> List[Tuple[int, int]]:
        if r == 0:
            return [(x, y)]
        out = []
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if max(abs(dx), abs(dy)) == r:
                    out.append((x + dx, y + dy))
        out.sort(key=lambda p: (abs(p[0] - x) + abs(p[1] - y), p[1], p[0]))
        return out

    def _touches_road(self, name: str, x: int, y: int) -> bool:
        w, h = FOOTPRINT[name]
        for dx in range(-1, w + 1):
            for dy in range(-1, h + 1):
                if 0 <= dx < w and 0 <= dy < h:
                    continue
                if (x + dx, y + dy) in self._roadset:
                    return True
        return False

    def road(self, owner: str, x: int, y: int) -> bool:
        if not self.canvas.inside(x, y):
            return False
        if (x, y) in self._blocked or (x, y) in self._roadset:
            return False
        if self.canvas.get(x, y) != ROCK:
            return False
        self._roadset.add((x, y))
        self.roads.append((owner, x, y))
        return True

    def unit(self, owner: str, name: str, x: int, y: int,
             mode: str = "Area Guard", health: int = 256, angle: int = 64) -> None:
        self.units.append(Unit(owner, name, x, y, angle, mode, health))
        self._unittiles.add((x, y))

    def free_tile(self, x: int, y: int, radius: int = 26,
                  terrain: Sequence[str] = DRIVEABLE) -> Tuple[int, int]:
        for r in range(radius + 1):
            for (tx, ty) in self._ring(x, y, r):
                if not self.canvas.inside(tx, ty):
                    continue
                if (tx, ty) in self._blocked or (tx, ty) in self._unittiles:
                    continue
                if self.canvas.get(tx, ty) in terrain:
                    return tx, ty
        raise ValueError(f"{self.filename}: no free unit tile near ({x},{y})")

    # -- serialisation -----------------------------------------------------
    def render(self) -> str:
        def pos(x: int, y: int) -> int:
            return y * self.size_x + x

        L: List[str] = []
        L.extend(self.header)
        L.append("")
        L.append("[BASIC]")
        for k, v in self.basic.items():
            L.append(f"{k}={v}")
        L.append("")
        L.append("[MAP]")
        L.append(f"SizeX={self.size_x}")
        L.append(f"SizeY={self.size_y}")
        for y, row in enumerate(self.canvas.rows()):
            L.append(f"{y:03d}={row}")
        L.append("")
        if self.choam:
            L.append("[CHOAM]")
            for k in sorted(self.choam):
                L.append(f"{k}={self.choam[k]}")
            L.append("")
        L.append("[STRUCTURES]")
        for i, s in enumerate(self.structures, start=1):
            L.append(f"ID{i:03d}={s.owner},{s.name},{s.health},{pos(s.x, s.y)}")
        for owner, x, y in self.roads:
            L.append(f"GEN{pos(x, y)}={owner},Road")
        L.append("")
        L.append("[UNITS]")
        for i, u in enumerate(self.units, start=1):
            L.append(f"ID{i:03d}={u.owner},{u.name},{u.health},"
                     f"{pos(u.x, u.y)},{u.angle},{u.mode}")
        L.append("")
        if self.reinforcements:
            L.append("[REINFORCEMENTS]")
            for i, r in enumerate(self.reinforcements, start=1):
                L.append(f"{i}={r}")
            L.append("")
        if self.teams:
            L.append("[TEAMS]")
            for i, t in enumerate(self.teams, start=1):
                L.append(f"{i}={t}")
            L.append("")
        for name, body in self.houses:
            L.append(f"[{name}]")
            for k, v in body.items():
                L.append(f"{k}={v}")
            L.append("")
        return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------
# Street painting
# ---------------------------------------------------------------------------
def street(sc: Scenario, owner: str, a: Tuple[int, int], b: Tuple[int, int],
           rng: Optional[Rng] = None, wiggle: int = 0) -> None:
    """Walk a street from a to b one tile at a time.

    `wiggle` is the percentage chance of taking the "wrong" axis on a step,
    which turns a straight run into the staircased, slightly drunk street
    lines that real Micropolis cities grew rather than a ruled grid line.
    """
    x, y = a
    bx, by = b
    sc.road(owner, x, y)
    guard = 0
    while (x, y) != (bx, by) and guard < 4096:
        guard += 1
        dx, dy = bx - x, by - y
        if dx == 0:
            step = (0, 1 if dy > 0 else -1)
        elif dy == 0:
            step = (1 if dx > 0 else -1, 0)
        else:
            horiz = abs(dx) >= abs(dy)
            if wiggle and rng is not None and rng.chance(wiggle):
                horiz = not horiz
            step = (1 if dx > 0 else -1, 0) if horiz else (0, 1 if dy > 0 else -1)
        x += step[0]
        y += step[1]
        sc.road(owner, x, y)


def street_path(sc: Scenario, owner: str, pts: Sequence[Tuple[int, int]],
                rng: Optional[Rng] = None, wiggle: int = 0) -> None:
    for i in range(len(pts) - 1):
        street(sc, owner, pts[i], pts[i + 1], rng, wiggle)


def street_grid(sc: Scenario, owner: str, x: int, y: int, w: int, h: int,
                pitch_x: int = 3, pitch_y: int = 3) -> None:
    """A small planned block -- used sparingly, for civic/market cores."""
    for gx in range(x, x + w + 1, pitch_x):
        for gy in range(y, y + h + 1):
            sc.road(owner, gx, gy)
    for gy in range(y, y + h + 1, pitch_y):
        for gx in range(x, x + w + 1):
            sc.road(owner, gx, gy)


def spurs(sc: Scenario, owner: str, spine: Sequence[Tuple[int, int]],
          rng: Rng, count: int, length: Tuple[int, int] = (4, 10)) -> None:
    """Dead-end side streets hanging off a spine -- organic, often useless."""
    if len(spine) < 2:
        return
    for _ in range(count):
        i = rng.rand(len(spine) - 1)
        ax, ay = spine[i]
        bx, by = spine[i + 1]
        t = rng.rand(101)
        px = ax + (bx - ax) * t // 100
        py = ay + (by - ay) * t // 100
        n = rng.between(*length)
        d = rng.pick([(1, 0), (-1, 0), (0, 1), (0, -1)])
        street(sc, owner, (px, py), (px + d[0] * n, py + d[1] * n), rng, 18)


# ---------------------------------------------------------------------------
# District zoning: hang 2x2 lots off street frontage
# ---------------------------------------------------------------------------
@dataclass
class District:
    """One zoning profile over a rectangle.

    `mix` is sampled per lot, so "RRRRC" is a residential street with the odd
    shop.  `fill` below 100 leaves vacant lots; that is the intended texture,
    not a defect.  `terrain` lets a district spill onto sand where the fiction
    wants a lot standing in a crater (isCityZoneTerrain allows sand/dunes as
    long as a rock tile anchors the footprint -- we keep at least one).
    """
    x: int
    y: int
    w: int
    h: int
    mix: str
    fill: int = 80
    limit: int = 10 ** 6
    health: int = 256


# candidate 2x2 lot origins that leave the given road tile on the lot frontage
_LOT_OFFSETS = ((1, -1), (1, 0), (-2, -1), (-2, 0),
                (-1, 1), (0, 1), (-1, -2), (0, -2))


def zone_district(sc: Scenario, owner: str, d: District, rng: Rng) -> int:
    """Attach lots to every street frontage inside the district rectangle."""
    placed = 0
    tiles = sorted(t for t in sc._roadset
                   if d.x <= t[0] < d.x + d.w and d.y <= t[1] < d.y + d.h)
    for (rx, ry) in tiles:
        if placed >= d.limit:
            break
        start = rng.rand(len(_LOT_OFFSETS))
        for k in range(len(_LOT_OFFSETS)):
            ox, oy = _LOT_OFFSETS[(start + k) % len(_LOT_OFFSETS)]
            lx, ly = rx + ox, ry + oy
            if not (d.x <= lx < d.x + d.w and d.y <= ly < d.y + d.h):
                continue
            if not sc.fits("Residential Zone", lx, ly):
                continue
            if not rng.chance(d.fill):
                continue
            code = d.mix[rng.rand(len(d.mix))]
            if code == '.':
                continue
            sc.place(owner, ZONE_NAME[code], lx, ly, d.health)
            placed += 1
            break
    return placed


def zone_districts(sc: Scenario, owner: str, districts: Sequence[District],
                   rng: Rng) -> int:
    return sum(zone_district(sc, owner, d, rng) for d in districts)


def anchors(sc: Scenario, owner: str, items: Sequence[Tuple[str, int, int]],
            health: int = 256, radius: int = 16, need_road: bool = True) -> None:
    for (name, x, y) in items:
        sc.place_near(owner, name, x, y, health, radius, need_road=need_road)


def turret_ring(sc: Scenario, owner: str, pts: Sequence[Tuple[int, int]],
                rng: Rng, rocket_every: int = 3) -> None:
    for i, (x, y) in enumerate(pts):
        name = "Rocket-Turret" if i % rocket_every == 0 else "Gun-Turret"
        try:
            sc.place_near(owner, name, x, y, 256, 6, need_road=False)
        except ValueError:
            continue


def walls(sc: Scenario, owner: str, pts: Sequence[Tuple[int, int]]) -> None:
    for (x, y) in pts:
        sc.try_place(owner, "Wall", x, y)


# Structure power draw -- config/ObjectData.ini.default. Negative entries
# produce; House compares producedPower against powerRequirement globally.
POWER_DRAW = {
    "Barracks": 10, "Const Yard": 0, "Gun-Turret": 10, "Heavy Factory": 35,
    "Hightech Factory": 35, "House IX": 40, "Light Factory": 20, "Palace": 100,
    "Outpost": 30, "Refinery": 30, "Repair Yard": 20, "Rocket-Turret": 25,
    "Spice Silo": 5, "Starport": 50, "Wall": 0, "Police Station": 20,
    "Stadium": 30, "Airport": 40, "WOR": 20, "Windtrap": 0,
    "Nuclear Plant": 0,
}
# ZonePower.h getZonePower, indexed by density 1..3.
ZONE_POWER = {
    "Residential Zone": (3, 7, 12),
    "Commercial Zone": (5, 11, 18),
    "Industrial Zone": (7, 14, 24),
}


def power_ledger(sc: Scenario, owner: str, level: int = 2) -> Tuple[int, int]:
    """(produced, required) for one house, assuming zones sit at `level`."""
    produced = 0
    required = 0
    for s in sc.structures:
        if s.owner != owner:
            continue
        if s.name == "Windtrap":
            produced += 100 * s.health // 256
        elif s.name == "Nuclear Plant":
            produced += 2000 * s.health // 256
        elif s.name in ZONE_POWER:
            required += ZONE_POWER[s.name][level - 1]
        else:
            required += POWER_DRAW.get(s.name, 0)
    return produced, required


def power_plan(sc: Scenario, owner: str, spots: Sequence[Tuple[int, int]],
               level: int = 2, margin: int = 115, cap: int = 40) -> int:
    """Top the house's wind farm up to `margin`% of its level-`level` draw.

    Zones are placed vacant (density 0) and draw nothing until they grow, so
    this is headroom for the city the mission expects to exist, not for the
    one on screen at cycle zero. Undersupplying is what the Blackout map does
    on purpose; every other map ships a grid that works.
    """
    added = 0
    idx = 0
    while added < cap:
        produced, required = power_ledger(sc, owner, level)
        if produced * 100 >= required * margin:
            break
        sx, sy = spots[idx % len(spots)]
        idx += 1
        try:
            sc.place_near(owner, "Windtrap", sx, sy, 256,
                          radius=10 + 2 * (idx // len(spots)), need_road=False)
        except ValueError:
            if idx > len(spots) * 6:
                break
            continue
        added += 1
    return added


def power_finish(sc: Scenario, level: int = 2, margin: int = 115,
                 skip: Sequence[str] = ()) -> Dict[str, int]:
    """Give every house a working grid, sampling wind-farm sites from its own
    footprint so traps end up spread through the town rather than clumped."""
    opening_precincts(sc)
    out: Dict[str, int] = {}
    for name, _ in sc.houses:
        if name in skip:
            continue
        sites = [(s.x, s.y) for s in sc.structures if s.owner == name]
        if not sites:
            continue
        step = max(1, len(sites) // 18)
        spots = sites[::step] or sites
        out[name] = power_plan(sc, name, spots, level, margin)
    return out


# Engine opening probes found unintended unrest in these non-crime missions.
# A few precincts protect their tactical premise; Ash retains neglected districts.
OPENING_PRECINCTS = {
    "Sihaya Basin": [("Atreides", 23, 30)],
    "Cielago Watch": [("Atreides", 38, 38)],
    "Hagal Flats": [("Atreides", 26, 50)],
    "Carthag Vise": [("Atreides", 40, 52), ("Harkonnen", 52, 14)],
    "Coriolis Gap": [("Sardaukar", 135, 54), ("Sardaukar", 114, 21),
                     ("Sardaukar", 120, 33)],
}


def opening_precincts(sc: Scenario) -> None:
    title = sc.filename.rsplit(" - ", 1)[-1][:-4]
    for owner, x, y in OPENING_PRECINCTS.get(title, []):
        sc.place_near(owner, "Police Station", x, y, radius=12)


def force(sc: Scenario, owner: str, cx: int, cy: int,
          loadout: Sequence[Tuple[str, str]], spread: int = 3) -> None:
    for i, (name, mode) in enumerate(loadout):
        gx = cx + ((i % 6) - 3) * spread
        gy = cy + ((i // 6) - 1) * spread
        ux, uy = sc.free_tile(gx, gy)
        sc.unit(owner, name, ux, uy, mode)


DEFAULT_CHOAM = {
    "Carryall": 2, "Harvester": 4, "Launcher": 5, "MCV": 2,
    "'Thopter": 4, "Quad": 5, "Siege Tank": 5, "Tank": 6, "Trike": 5,
}


def basic(win: int, lose: int, timeout: int = 0, tech: int = 8,
          brief: str = "ATTACK.WSA", winpic: str = "WIN1.WSA") -> Dict[str, object]:
    return {
        "Version": 2,
        "License": "CC-BY-SA",
        "Author": "DuneCity",
        "TechLevel": tech,
        "WinFlags": win,
        "LoseFlags": lose,
        "LosePicture": "LOSTBILD.WSA",
        "WinPicture": winpic,
        "BriefPicture": brief,
        "TimeOut": timeout,
    }


# ===========================================================================
# 1 -- Sihaya Basin: cramped shelf, colonise separated rock
# ===========================================================================
def scenario_sihaya_basin() -> Scenario:
    W = H = 128
    rng = Rng(0x51A1A)
    c = Canvas(W, H)

    # The home shelf is deliberately tiny: everything the town has is already
    # on it, and the only way to grow is to drive an MCV across open sand.
    c.organic(22, 30, 11, ROCK, rng, lobes=6)
    c.ribbon([(14, 40), (20, 46), (26, 48)], ROCK, 4, rng)       # short apron
    c.ribbon([(10, 20), (16, 14), (26, 12), (34, 16)], MOUNTAIN, 5, rng)

    # Four genuinely reachable destination islands, each with its own spice.
    c.organic(60, 20, 14, ROCK, rng, lobes=8)
    c.organic(44, 78, 13, ROCK, rng, lobes=8)
    c.organic(92, 98, 16, ROCK, rng, lobes=9)
    c.organic(24, 96, 11, ROCK, rng, lobes=6)

    # Harkonnen hold the largest shelf and the best spice belt.
    c.organic(98, 44, 21, ROCK, rng, lobes=10)
    c.ribbon([(74, 24), (78, 46), (74, 66)], MOUNTAIN, 6, rng)
    c.rect(74, 40, 8, 8, SAND)                                    # the one gap

    for (sx, sy, r, rich) in [(40, 34, 8, False), (56, 40, 9, True),
                              (70, 88, 10, True), (36, 60, 8, False),
                              (14, 66, 7, False), (108, 20, 9, True),
                              (110, 78, 8, True), (62, 112, 9, False),
                              (20, 112, 7, False), (88, 12, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(48, 56), (80, 74), (34, 12), (104, 118)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(52, 48, BLOOM)
    c.set(38, 90, BLOOM)
    c.set(86, 30, BLOOM)

    sc = Scenario(
        filename="2P - 128x128 - Sihaya Basin.ini",
        size_x=W, size_y=H,
        header=[
            "; Sihaya Basin -- 128x128, 2 houses, DuneCity city-sim scenario.",
            ";",
            "; The Basin township outgrew its shelf two generations ago. Every",
            "; buildable metre of rock is already under a lot, the refinery",
            "; queue is longer than the harvester fleet, and House Harkonnen",
            "; sits on the wide eastern shelf with room the Atreides do not",
            "; have. Two mothballed MCVs are all the expansion budget CHOAM",
            "; would release. Drive them out to the open rock islands, plant",
            "; real towns there, then break the Harkonnen shelf.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Opponent: Harkonnen -- larger city, larger army, better spice.",
            "; Win     : destroy the Harkonnen. Lose: your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="ATTACK.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 7000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 11000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- the cramped home town ------------------------------------------
    street_path(sc, "Atreides", [(15, 26), (22, 27), (30, 30)], rng, 25)
    street_path(sc, "Atreides", [(17, 34), (24, 35), (31, 33)], rng, 25)
    street_path(sc, "Atreides", [(22, 22), (23, 38)], rng, 12)
    street_path(sc, "Atreides", [(18, 40), (26, 44), (26, 48)], rng, 20)
    spurs(sc, "Atreides", [(15, 26), (30, 30), (31, 33), (17, 34)], rng, 5, (3, 6))

    anchors(sc, "Atreides", [
        ("Const Yard", 21, 29), ("Refinery", 25, 26), ("Windtrap", 18, 27),
        ("Windtrap", 19, 33), ("Light Factory", 27, 33), ("Spice Silo", 16, 30),
        ("Outpost", 24, 32), ("Barracks", 20, 37), ("Repair Yard", 25, 44),
    ])
    zone_districts(sc, "Atreides", [
        District(13, 22, 22, 10, "RRRCCRI", fill=88, limit=26),
        District(13, 32, 22, 9, "RRCCRII", fill=80, limit=18),
        District(14, 40, 16, 10, "RCCI", fill=70, limit=10),
    ], rng)
    turret_ring(sc, "Atreides", [(14, 24), (30, 24), (32, 34), (16, 40)], rng)

    force(sc, "Atreides", 24, 40, [
        ("MCV", "Area Guard"), ("MCV", "Area Guard"),
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ])

    # -- Harkonnen eastern shelf ----------------------------------------
    street_path(sc, "Harkonnen", [(84, 34), (98, 36), (112, 40)], rng, 18)
    street_path(sc, "Harkonnen", [(86, 46), (100, 48), (114, 50)], rng, 18)
    street_path(sc, "Harkonnen", [(88, 58), (102, 58), (112, 56)], rng, 18)
    street_path(sc, "Harkonnen", [(98, 30), (99, 62)], rng, 10)
    street_path(sc, "Harkonnen", [(88, 32), (86, 60)], rng, 14)
    spurs(sc, "Harkonnen", [(84, 34), (112, 40), (114, 50), (88, 58)], rng, 7)

    anchors(sc, "Harkonnen", [
        ("Const Yard", 96, 38), ("Refinery", 92, 34), ("Refinery", 104, 48),
        ("Heavy Factory", 100, 42), ("Light Factory", 90, 44),
        ("Hightech Factory", 104, 36), ("Repair Yard", 94, 54),
        ("Windtrap", 88, 36), ("Windtrap", 88, 40), ("Windtrap", 108, 44),
        ("Windtrap", 106, 54), ("Spice Silo", 110, 40), ("Spice Silo", 110, 46),
        ("Outpost", 96, 46), ("House IX", 102, 54), ("Barracks", 86, 52),
        ("WOR", 108, 58), ("Starport", 92, 60),
    ])
    zone_districts(sc, "Harkonnen", [
        District(82, 30, 34, 12, "IIRC", fill=78, limit=26),
        District(82, 42, 34, 10, "RRCCIP", fill=82, limit=28),
        District(82, 52, 34, 12, "RRRCI", fill=78, limit=26),
    ], rng)
    turret_ring(sc, "Harkonnen", [(82, 32), (92, 28), (108, 30), (116, 44),
                                  (114, 58), (98, 64), (84, 58)], rng)
    force(sc, "Harkonnen", 96, 50, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Devastator", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,5",
        "Harkonnen,Staging,Wheeled,3,6",
        "Harkonnen,Normal,Tracked,4,9",
        "Harkonnen,Kamikaze,Tracked,2,5",
    ]
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,8",
        "Harkonnen,Quad,Enemybase,8",
        "Harkonnen,Tank,Enemybase,17",
        "Harkonnen,Launcher,Enemybase,17",
        "Harkonnen,Siege Tank,Enemybase,27",
        "Harkonnen,Tank,Enemybase,27",
        "Harkonnen,Tank,Enemybase,38,+",
        "Harkonnen,Launcher,Enemybase,38,+",
        # CHOAM releases a third colony hull once the first two are committed.
        "Atreides,Harvester,Homebase,10",
        "Atreides,MCV,Homebase,19",
        "Atreides,Harvester,Homebase,24",
        "Atreides,Quad,Homebase,31,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 2 -- Ash Quarter: neglected slum, real gang-outbreak conditions
# ===========================================================================
def scenario_ash_quarter() -> Scenario:
    W = H = 128
    rng = Rng(0xA5401)
    c = Canvas(W, H)

    # A single long plateau running north-west to south-east. Length matters:
    # land value is (34 - distanceToClusterCentroid/2)*4, so the far end of a
    # 90-tile city is already starved before pollution is counted.
    c.ribbon([(20, 24), (36, 40), (54, 58), (74, 76), (94, 96), (104, 110)],
             ROCK, 26, rng)
    c.ribbon([(24, 18), (42, 30), (60, 48)], ROCK, 12, rng)   # northern works
    c.organic(100, 24, 13, ROCK, rng, lobes=7)                # raider outcrop
    c.ribbon([(66, 18), (78, 34), (84, 52)], MOUNTAIN, 7, rng)
    c.rect(74, 34, 7, 6, ROCK)                                # the one north gate
    c.organic(116, 48, 11, ROCK, rng, lobes=6)                # raider forward camp

    # Shelled ground: sand craters punched through the plateau. Zones may sit
    # on sand but roads may not, so craters read as permanent scars.
    for _ in range(26):
        cx = rng.between(24, 100)
        cy = rng.between(30, 108)
        c.organic(cx, cy, rng.between(2, 4), SAND, rng, lobes=3, only_if=(ROCK,))

    for (sx, sy, r, rich) in [(112, 62, 9, True), (116, 108, 9, True),
                              (12, 46, 9, True), (40, 96, 8, False),
                              (14, 96, 7, False), (86, 10, 8, False),
                              (118, 34, 7, False), (60, 116, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(96, 66), (16, 12), (120, 84)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(110, 48, BLOOM)
    c.set(10, 62, BLOOM)

    sc = Scenario(
        filename="3P - 128x128 - Ash Quarter.ini",
        size_x=W, size_y=H,
        header=[
            "; Ash Quarter -- 128x128, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; Micropolis Detroit 1972, moved to Arrakis: land values collapse,",
            "; industry stays, policing does not. The Quarter is one long",
            "; ribbon town. The works and both surviving precincts are at the",
            "; northern end; the south-east tenements sit as far from the city",
            "; centre as the plateau allows, rock-locked with no open sand to",
            "; lift their land value, boxed in by heavy industry, and with",
            "; Mercenary scavengers squatting the block next door. That is the",
            "; recipe the crime scan actually reads, and the gangs it produces",
            "; are real Trooper squads that hunt your buildings.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen raiders (north outcrop), Mercenary",
            ";           scavengers squatting inside the south-east slum.",
            "; Win     : clear both hostile houses. Lose: your buildings gone.",
            "; Police the slum, re-zone it, or watch it burn -- your call.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="MACHINE.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 8000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 15000, "Quota": 0}),
            ("Mercenary", {"Brain": "Team2", "Credits": 8000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- northern works and civic end (policed, functioning) -------------
    street_path(sc, "Atreides", [(22, 26), (34, 34), (44, 42), (56, 54)], rng, 22)
    street_path(sc, "Atreides", [(18, 34), (30, 42), (42, 50), (54, 62)], rng, 22)
    street_grid(sc, "Atreides", 26, 22, 18, 12, 3, 3)
    spurs(sc, "Atreides", [(22, 26), (44, 42), (56, 54), (18, 34)], rng, 6)

    anchors(sc, "Atreides", [
        ("Const Yard", 30, 30), ("Refinery", 34, 26), ("Heavy Factory", 38, 32),
        ("Hightech Factory", 28, 24), ("Light Factory", 24, 34),
        ("Windtrap", 26, 28), ("Windtrap", 40, 26), ("Windtrap", 22, 30),
        ("Spice Silo", 20, 32), ("Repair Yard", 42, 38), ("Outpost", 32, 36),
        ("House IX", 36, 42), ("Police Station", 30, 38),
        ("Police Station", 40, 30), ("Barracks", 26, 42), ("Airport", 46, 46),
    ], health=210)
    zone_districts(sc, "Atreides", [
        District(16, 20, 30, 12, "IIICR", fill=84, limit=22),
        District(16, 32, 34, 12, "RRCCIP", fill=82, limit=26),
        District(30, 44, 34, 14, "RRRCI", fill=78, limit=22),
    ], rng)

    # -- the middle: market street, still just about working -------------
    street_path(sc, "Atreides", [(56, 58), (68, 70), (78, 80)], rng, 20)
    street_path(sc, "Atreides", [(52, 66), (64, 76), (74, 86)], rng, 20)
    spurs(sc, "Atreides", [(56, 58), (78, 80), (52, 66)], rng, 5)
    anchors(sc, "Atreides", [
        ("Refinery", 62, 68), ("Spice Silo", 58, 64), ("Windtrap", 66, 64),
        ("Outpost", 70, 74), ("WOR", 60, 78),
    ], health=200)
    zone_districts(sc, "Atreides", [
        District(48, 54, 36, 34, "RRCCRI", fill=80, limit=34),
    ], rng)

    # -- the slum: dense, unpoliced, ringed by heavy industry ------------
    # Streets are tight and well connected inside the slum (R->C traffic must
    # succeed for the tenements to fill up); what is missing is a precinct.
    street_path(sc, "Atreides", [(80, 84), (92, 96), (102, 108)], rng, 16)
    street_path(sc, "Atreides", [(86, 82), (98, 94), (106, 104)], rng, 16)
    street_path(sc, "Atreides", [(78, 92), (90, 102), (100, 112)], rng, 16)
    street_path(sc, "Atreides", [(84, 90), (96, 90)], rng, 10)
    street_path(sc, "Atreides", [(92, 84), (92, 110)], rng, 14)
    street_path(sc, "Atreides", [(100, 88), (100, 112)], rng, 14)
    spurs(sc, "Atreides", [(80, 84), (102, 108), (78, 92), (106, 104)], rng, 8)

    # The industrial collar: pollution radius is 5, and these sit right on the
    # tenements rather than across a buffer. That is the whole problem.
    anchors(sc, "Atreides", [
        ("Heavy Factory", 84, 86), ("Heavy Factory", 104, 100),
        ("Refinery", 88, 106), ("Light Factory", 96, 84),
        ("Light Factory", 78, 98), ("Repair Yard", 106, 92),
        ("Windtrap", 82, 92), ("Windtrap", 98, 108), ("Windtrap", 90, 84),
        ("Outpost", 94, 98), ("Barracks", 104, 110),
    ], health=190)
    zone_districts(sc, "Atreides", [
        District(74, 80, 38, 34, "RRRRRRRC", fill=94, limit=54),
        District(74, 80, 38, 34, "RRRRIC", fill=88, limit=22),
    ], rng)
    # No police station anywhere south of y=60, and no turrets in the slum --
    # gun and rocket turrets each carry 15% police coverage.
    turret_ring(sc, "Atreides", [(18, 22), (44, 20), (50, 50), (20, 44)], rng)

    force(sc, "Atreides", 36, 40, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"),
        ("Tank", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])

    # -- Mercenary squatters, deliberately inside the slum ---------------
    # hostileLandValuePenalty reaches four tiles and takes up to 80 off land
    # value, which is exactly the last shove the crime formula needs.
    street_path(sc, "Mercenary", [(94, 116), (108, 116)], rng, 10)
    street_path(sc, "Mercenary", [(98, 112), (99, 122)], rng, 10)
    anchors(sc, "Mercenary", [
        ("Const Yard", 100, 116), ("Windtrap", 104, 118),
        ("Light Factory", 96, 118), ("Outpost", 108, 114),
        ("Refinery", 104, 122), ("Heavy Factory", 94, 122),
        ("Barracks", 108, 120),
    ], radius=12)
    zone_districts(sc, "Mercenary", [
        District(90, 110, 28, 18, "IRRC", fill=78, limit=18),
    ], rng)
    force(sc, "Mercenary", 104, 120, [
        # A small gang headquarters, not an army that razes the slum before
        # its unrest timer can mature. Most local pressure comes from crime.
        ("Harvester", "Harvest"), ("Raider Trike", "Guard"),
    ])

    # -- Harkonnen raider outcrop ----------------------------------------
    street_path(sc, "Harkonnen", [(92, 20), (104, 24), (110, 32)], rng, 18)
    street_path(sc, "Harkonnen", [(94, 30), (106, 30)], rng, 12)
    street_path(sc, "Harkonnen", [(100, 16), (101, 34)], rng, 10)
    spurs(sc, "Harkonnen", [(92, 20), (110, 32), (94, 30)], rng, 5)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 100, 24), ("Refinery", 104, 20), ("Heavy Factory", 96, 28),
        ("Windtrap", 94, 22), ("Windtrap", 108, 28), ("Repair Yard", 104, 32),
        ("Outpost", 100, 30), ("Barracks", 92, 26), ("Spice Silo", 110, 22),
        ("Hightech Factory", 108, 18), ("Starport", 92, 32), ("WOR", 96, 16),
    ])
    zone_districts(sc, "Harkonnen", [
        District(88, 12, 30, 28, "IIRCC", fill=82, limit=34),
    ], rng)
    turret_ring(sc, "Harkonnen", [(90, 16), (110, 16), (114, 32), (92, 36)], rng)
    force(sc, "Harkonnen", 102, 36, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Carryall", "Area Guard"), ("Devastator", "Area Guard"),
    ])

    # Harkonnen forward camp on the eastern sand, level with the slum: the
    # raiders do not have to come through the northern gate any more.
    street_path(sc, "Harkonnen", [(110, 46), (120, 48), (124, 54)], rng, 16)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 118, 48), ("Refinery", 114, 44), ("Heavy Factory", 120, 52),
        ("Windtrap", 112, 50), ("Windtrap", 122, 44), ("Repair Yard", 116, 54),
        ("Outpost", 120, 44), ("Barracks", 110, 50),
    ], radius=12)
    zone_districts(sc, "Harkonnen", [
        District(106, 38, 26, 22, "IIRC", fill=76, limit=18),
    ], rng)
    turret_ring(sc, "Harkonnen", [(108, 42), (126, 42), (126, 58), (108, 58)], rng)
    force(sc, "Harkonnen", 118, 58, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,5",
        "Harkonnen,Normal,Tracked,4,8",
        "Harkonnen,Kamikaze,Tracked,3,6",
        "Mercenary,Guard,Foot,2,4",
        "Mercenary,Normal,Wheeled,3,7",
    ]
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,6",
        "Harkonnen,Quad,Enemybase,6",
        "Harkonnen,Tank,Enemybase,15",
        "Harkonnen,Launcher,Enemybase,15",
        "Harkonnen,Siege Tank,Enemybase,26",
        "Harkonnen,Tank,Enemybase,34,+",
        "Mercenary,Raider Trike,Enemybase,11",
        "Mercenary,Troopers,Enemybase,20,+",
        "Atreides,Harvester,Homebase,9",
        "Atreides,Quad,Homebase,14",
        "Atreides,Launcher,Homebase,25,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 3 -- Coriolis Gap: assault an entrenched Sardaukar city
# ===========================================================================
def scenario_coriolis_gap() -> Scenario:
    W, H = 160, 96
    rng = Rng(0xC0B15)
    c = Canvas(W, H)

    c.organic(28, 48, 20, ROCK, rng, lobes=9)                # Atreides shelf
    c.organic(76, 46, 16, ROCK, rng, lobes=8)                # contested middle
    c.ribbon([(112, 16), (124, 46), (120, 78)], ROCK, 34, rng)  # Sardaukar city

    c.ribbon([(52, 8), (56, 40), (50, 60), (56, 88)], MOUNTAIN, 8, rng)
    c.ribbon([(96, 6), (100, 40), (96, 70), (100, 90)], MOUNTAIN, 8, rng)
    c.rect(50, 44, 10, 7, ROCK)        # west pass
    c.rect(94, 30, 10, 6, ROCK)        # north gate into the Sardaukar city
    c.rect(94, 62, 10, 6, ROCK)        # south gate

    for (sx, sy, r, rich) in [(20, 16, 9, True), (14, 80, 9, True),
                              (72, 14, 8, False), (70, 80, 8, False),
                              (140, 12, 9, True), (148, 84, 9, True),
                              (44, 26, 7, False), (44, 70, 7, False),
                              (84, 46, 6, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(66, 60), (130, 60), (36, 88)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(64, 26, BLOOM)
    c.set(86, 72, BLOOM)

    sc = Scenario(
        filename="3P - 160x96 - Coriolis Gap.ini",
        size_x=W, size_y=H,
        header=[
            "; Coriolis Gap -- 160x96, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; The Sardaukar built a garrison city behind the eastern wall and",
            "; have had years to fortify it: more production than you, more",
            "; credits than you, a Palace, and turrets on both gates. Your own",
            "; western shelf is a working town, not an army. Harkonnen hold the",
            "; middle plateau and the western pass on the same side as the",
            "; Sardaukar. You cannot out-build them from a standing start;",
            "; take the middle, take a gate, then take the city.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Sardaukar garrison city (east), Harkonnen (middle).",
            "; Win     : clear both hostile houses. Lose: your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="BLOODY.WSA", winpic="WIN2.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 16000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 8000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- Atreides western shelf ------------------------------------------
    street_path(sc, "Atreides", [(14, 40), (26, 42), (40, 44)], rng, 20)
    street_path(sc, "Atreides", [(14, 52), (28, 54), (42, 52)], rng, 20)
    street_path(sc, "Atreides", [(26, 34), (27, 62)], rng, 12)
    street_path(sc, "Atreides", [(36, 36), (38, 60)], rng, 16)
    spurs(sc, "Atreides", [(14, 40), (40, 44), (14, 52), (42, 52)], rng, 7)

    anchors(sc, "Atreides", [
        ("Const Yard", 24, 46), ("Refinery", 20, 42), ("Heavy Factory", 30, 44),
        ("Light Factory", 18, 52), ("Windtrap", 22, 50), ("Windtrap", 28, 40),
        ("Windtrap", 34, 50), ("Spice Silo", 16, 46), ("Repair Yard", 32, 54),
        ("Outpost", 26, 52), ("Barracks", 20, 58), ("Police Station", 30, 50),
        ("Hightech Factory", 36, 42),
    ])
    zone_districts(sc, "Atreides", [
        District(10, 32, 36, 12, "IICR", fill=80, limit=18),
        District(10, 44, 36, 10, "RRCCIP", fill=84, limit=26),
        District(10, 54, 36, 14, "RRRCI", fill=78, limit=22),
    ], rng)
    turret_ring(sc, "Atreides", [(12, 38), (26, 30), (42, 38), (44, 56), (24, 66)], rng)
    force(sc, "Atreides", 34, 58, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])

    # -- Harkonnen middle plateau ----------------------------------------
    street_path(sc, "Harkonnen", [(66, 40), (80, 42), (88, 48)], rng, 18)
    street_path(sc, "Harkonnen", [(68, 52), (82, 54)], rng, 16)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 74, 44), ("Refinery", 78, 40), ("Heavy Factory", 70, 48),
        ("Windtrap", 72, 40), ("Windtrap", 84, 46), ("Repair Yard", 80, 52),
        ("Outpost", 76, 50), ("Barracks", 68, 44),
    ])
    zone_districts(sc, "Harkonnen", [
        District(62, 34, 30, 26, "IIRC", fill=76, limit=24),
    ], rng)
    turret_ring(sc, "Harkonnen", [(62, 38), (88, 38), (90, 56), (64, 56),
                                  (54, 46), (58, 48)], rng)
    force(sc, "Harkonnen", 78, 56, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Devastator", "Area Guard"),
    ])

    # -- Sardaukar garrison city (the assault target) --------------------
    street_path(sc, "Sardaukar", [(110, 20), (122, 24), (134, 22)], rng, 16)
    street_path(sc, "Sardaukar", [(108, 34), (124, 36), (140, 34)], rng, 16)
    street_path(sc, "Sardaukar", [(108, 48), (126, 48), (142, 50)], rng, 16)
    street_path(sc, "Sardaukar", [(110, 62), (126, 62), (140, 64)], rng, 16)
    street_path(sc, "Sardaukar", [(110, 76), (128, 76), (138, 74)], rng, 16)
    street_path(sc, "Sardaukar", [(118, 18), (120, 80)], rng, 8)
    street_path(sc, "Sardaukar", [(132, 20), (134, 78)], rng, 8)
    spurs(sc, "Sardaukar", [(110, 20), (142, 50), (110, 76), (140, 34)], rng, 9)

    anchors(sc, "Sardaukar", [
        ("Const Yard", 120, 34), ("Const Yard", 128, 62),
        ("Refinery", 114, 28), ("Refinery", 136, 52), ("Refinery", 124, 70),
        ("Heavy Factory", 124, 32), ("Heavy Factory", 116, 60),
        ("Hightech Factory", 130, 36), ("Light Factory", 112, 44),
        ("Repair Yard", 134, 62), ("Repair Yard", 114, 70),
        ("Starport", 138, 40), ("Palace", 126, 46),
        ("Windtrap", 110, 24), ("Windtrap", 116, 38), ("Windtrap", 130, 28),
        ("Windtrap", 138, 58), ("Windtrap", 112, 66), ("Windtrap", 132, 72),
        ("Spice Silo", 140, 30), ("Spice Silo", 108, 52),
        ("Outpost", 122, 40), ("Outpost", 130, 66), ("House IX", 134, 46),
        ("Barracks", 112, 76), ("WOR", 138, 68), ("Airport", 118, 50),
        ("Police Station", 124, 56), ("Police Station", 128, 26),
    ])
    zone_districts(sc, "Sardaukar", [
        District(104, 14, 48, 20, "IIRCC", fill=82, limit=30),
        District(104, 34, 48, 16, "RRCCIP", fill=84, limit=34),
        District(104, 50, 48, 16, "RRRCI", fill=82, limit=32),
        District(104, 66, 48, 18, "RRCCI", fill=80, limit=28),
    ], rng)
    turret_ring(sc, "Sardaukar", [
        (100, 30), (104, 34), (100, 66), (104, 62),
        (106, 18), (120, 12), (140, 16), (150, 40), (150, 62),
        (136, 82), (114, 84), (106, 72),
    ], rng, rocket_every=2)
    force(sc, "Sardaukar", 122, 54, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Troopers", "Area Guard"),
    ])
    force(sc, "Sardaukar", 100, 33, [
        ("Troopers", "Guard"), ("Troopers", "Guard"), ("Launcher", "Guard"),
        ("Tank", "Guard"),
    ], spread=2)
    force(sc, "Sardaukar", 100, 65, [
        ("Troopers", "Guard"), ("Troopers", "Guard"), ("Launcher", "Guard"),
        ("Tank", "Guard"),
    ], spread=2)

    sc.teams = [
        "Sardaukar,Guard,Foot,3,7",
        "Sardaukar,Guard,Tracked,2,5",
        "Sardaukar,Normal,Tracked,4,9",
        "Sardaukar,Staging,Wheeled,3,6",
        "Harkonnen,Guard,Foot,2,4",
        "Harkonnen,Normal,Tracked,3,7",
    ]
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,7",
        "Harkonnen,Quad,Enemybase,7",
        "Harkonnen,Tank,Enemybase,16",
        "Sardaukar,Troopers,Homebase,12,+",
        "Sardaukar,Tank,Enemybase,22",
        "Sardaukar,Launcher,Enemybase,22",
        "Sardaukar,Siege Tank,Enemybase,33",
        "Sardaukar,Tank,Enemybase,44,+",
        "Atreides,Harvester,Homebase,11",
        "Atreides,Tank,Homebase,20",
        "Atreides,Siege Tank,Homebase,30,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 4 -- Cielago Watch: 30-minute holdout
# ===========================================================================
def scenario_cielago_watch() -> Scenario:
    W = H = 96
    rng = Rng(0xC1E10)
    c = Canvas(W, H)

    # A mesa with two approach ramps and nothing else. Small on purpose.
    c.organic(46, 46, 16, ROCK, rng, lobes=8)
    c.ribbon([(30, 44), (16, 40), (6, 36)], ROCK, 5, rng)     # west ramp
    c.ribbon([(60, 52), (74, 58), (86, 62)], ROCK, 5, rng)    # east ramp
    c.ribbon([(24, 24), (40, 16), (58, 18), (72, 28)], MOUNTAIN, 7, rng)
    c.ribbon([(22, 72), (40, 82), (60, 80), (74, 70)], MOUNTAIN, 7, rng)

    c.organic(14, 62, 10, ROCK, rng, lobes=5)                 # Harkonnen camp
    c.organic(82, 30, 11, ROCK, rng, lobes=6)                 # Ordos camp

    for (sx, sy, r, rich) in [(36, 34, 7, True), (58, 62, 7, True),
                              (10, 20, 8, False), (88, 84, 8, False),
                              (26, 88, 7, False), (88, 8, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    c.set(48, 30, BLOOM)

    sc = Scenario(
        filename="3P - 96x96 - Cielago Watch.ini",
        size_x=W, size_y=H,
        header=[
            "; Cielago Watch -- 96x96, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; A relay township on an isolated mesa, two ramps, no depth. The",
            "; Harkonnen and the Ordos both want the watch tower and neither",
            "; needs it intact. CHOAM will lift the garrison out in thirty",
            "; minutes. Until then the Watch holds -- or it does not.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (south-west), Ordos (north-east).",
            "; Win     : still holding buildings when the clock runs out.",
            "; Lose    : your buildings gone before then.",
            "; WinFlags=10 (player-has-buildings + timeout), LoseFlags=8 makes",
            "; the timeout the victory condition (TimeoutTrigger::trigger).",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(10, 8, timeout=30, brief="SARDUKAR.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 6500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 12000, "Quota": 0}),
            ("Ordos", {"Brain": "Team2", "Credits": 12000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- the Watch --------------------------------------------------------
    street_path(sc, "Atreides", [(36, 40), (46, 42), (56, 44)], rng, 18)
    street_path(sc, "Atreides", [(36, 50), (46, 52), (58, 52)], rng, 18)
    street_path(sc, "Atreides", [(44, 36), (45, 58)], rng, 10)
    street_path(sc, "Atreides", [(54, 38), (55, 56)], rng, 10)
    spurs(sc, "Atreides", [(36, 40), (56, 44), (36, 50)], rng, 4, (3, 6))

    anchors(sc, "Atreides", [
        ("Const Yard", 44, 44), ("Refinery", 48, 40), ("Windtrap", 40, 42),
        ("Windtrap", 40, 50), ("Windtrap", 54, 50), ("Light Factory", 50, 48),
        ("Heavy Factory", 52, 42), ("Repair Yard", 42, 54),
        ("Spice Silo", 38, 46), ("Outpost", 48, 52), ("Barracks", 56, 46),
        ("Police Station", 46, 48),
    ])
    zone_districts(sc, "Atreides", [
        District(32, 34, 30, 12, "RRCI", fill=78, limit=16),
        District(32, 46, 30, 16, "RRCCI", fill=80, limit=18),
    ], rng)
    turret_ring(sc, "Atreides", [
        (32, 42), (32, 50), (34, 36), (58, 40), (60, 50), (46, 34), (46, 60),
    ], rng, rocket_every=2)
    walls(sc, "Atreides", [(33, 44), (33, 45), (33, 46), (33, 47),
                           (59, 46), (59, 47), (59, 48), (59, 49)])
    force(sc, "Atreides", 46, 56, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Troopers", "Area Guard"),
        ("Troopers", "Area Guard"), ("Carryall", "Area Guard"),
    ])

    # -- Harkonnen siege camp --------------------------------------------
    street_path(sc, "Harkonnen", [(8, 58), (18, 60), (22, 66)], rng, 16)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 14, 60), ("Refinery", 10, 56), ("Heavy Factory", 18, 62),
        ("Windtrap", 10, 62), ("Windtrap", 20, 58), ("Repair Yard", 14, 66),
        ("Barracks", 8, 64), ("Outpost", 18, 56),
    ])
    zone_districts(sc, "Harkonnen", [
        District(4, 52, 24, 20, "IIRC", fill=72, limit=14),
    ], rng)
    force(sc, "Harkonnen", 20, 64, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Devastator", "Area Guard"),
    ])

    # -- Ordos siege camp -------------------------------------------------
    street_path(sc, "Ordos", [(76, 26), (86, 28), (90, 34)], rng, 16)
    anchors(sc, "Ordos", [
        ("Const Yard", 82, 28), ("Refinery", 86, 24), ("Heavy Factory", 78, 30),
        ("Windtrap", 78, 24), ("Windtrap", 88, 32), ("Repair Yard", 84, 34),
        ("Barracks", 90, 26), ("Outpost", 82, 34),
    ])
    zone_districts(sc, "Ordos", [
        District(72, 20, 24, 20, "IIRC", fill=72, limit=14),
    ], rng)
    force(sc, "Ordos", 80, 36, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Raider Trike", "Area Guard"), ("Deviator", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Normal,Tracked,4,8",
        "Harkonnen,Kamikaze,Tracked,3,6",
        "Harkonnen,Guard,Foot,2,4",
        "Ordos,Normal,Wheeled,4,8",
        "Ordos,Kamikaze,Tracked,3,6",
        "Ordos,Guard,Foot,2,4",
    ]
    # Waves land roughly every four minutes and get heavier; the last two
    # repeat, so the final ten minutes are the real test.
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,4",
        "Ordos,Raider Trike,Enemybase,4",
        "Harkonnen,Quad,Enemybase,8",
        "Ordos,Quad,Enemybase,8",
        "Harkonnen,Tank,Enemybase,12",
        "Ordos,Tank,Enemybase,12",
        "Harkonnen,Launcher,Enemybase,16",
        "Ordos,Siege Tank,Enemybase,16",
        "Harkonnen,Siege Tank,Enemybase,20,+",
        "Ordos,Launcher,Enemybase,20,+",
        "Harkonnen,Tank,Enemybase,24,+",
        "Ordos,Tank,Enemybase,24,+",
        "Atreides,Troopers,Homebase,7",
        "Atreides,Tank,Homebase,13",
        "Atreides,Launcher,Homebase,19,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 5 -- Hagal Flats: spice quota with real storage expansion
# ===========================================================================
def scenario_hagal_flats() -> Scenario:
    W = H = 128
    rng = Rng(0x4A6A1)
    c = Canvas(W, H)

    # Wide open harvesting country: small rock islands, enormous spice.
    c.organic(26, 62, 14, ROCK, rng, lobes=7)                 # player town
    c.organic(58, 30, 10, ROCK, rng, lobes=5)                 # silo outstation
    c.organic(62, 96, 11, ROCK, rng, lobes=6)                 # southern outpost
    c.organic(104, 40, 15, ROCK, rng, lobes=8)                # Ordos
    c.organic(100, 98, 13, ROCK, rng, lobes=7)                # Harkonnen
    c.ribbon([(46, 6), (52, 30), (46, 54)], MOUNTAIN, 6, rng)
    c.rect(44, 28, 10, 6, SAND)

    for (sx, sy, r, rich) in [(44, 76, 12, True), (76, 60, 13, True),
                              (32, 30, 11, True), (86, 18, 10, True),
                              (20, 100, 10, True), (110, 70, 11, True),
                              (76, 110, 10, False), (14, 44, 9, False),
                              (120, 14, 9, False), (60, 12, 8, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(60, 70), (92, 84), (30, 14)]:
        c.organic(dx, dy, 7, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(48, 62, BLOOM)
    c.set(90, 54, BLOOM)
    c.set(36, 108, BLOOM)

    sc = Scenario(
        filename="3P - 128x128 - Hagal Flats.ini",
        size_x=W, size_y=H,
        header=[
            "; Hagal Flats -- 128x128, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; A CHOAM delivery contract, not a war. The Flats are the richest",
            "; open sand on this latitude and your refinery town is the only",
            "; one on them -- for now. Ordos and Harkonnen are working the same",
            "; fields and will raid the convoy routes. You need 24000 credits",
            "; of refined spice in storage. Your silo and two refineries hold",
            "; only 12010, so add storage as well as harvesters. The unfinished",
            "; outstations offer room, but their exposed supply routes need",
            "; protection while the rival houses raid the fields.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Ordos (east), Harkonnen (south-east).",
            "; Win     : Quota=24000 stored credits (WinFlags bit 4).",
            "; Lose    : your buildings gone. Killing the AI is not a win here.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(6, 4, brief="HARVEST.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 4500, "Quota": 24000}),
            ("Ordos", {"Brain": "Team2", "Credits": 9000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 9000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- Atreides refinery town -------------------------------------------
    street_path(sc, "Atreides", [(16, 56), (26, 58), (36, 60)], rng, 20)
    street_path(sc, "Atreides", [(16, 66), (28, 68), (38, 66)], rng, 20)
    street_path(sc, "Atreides", [(26, 52), (27, 72)], rng, 12)
    spurs(sc, "Atreides", [(16, 56), (36, 60), (16, 66)], rng, 5)

    anchors(sc, "Atreides", [
        ("Const Yard", 24, 60), ("Refinery", 28, 56), ("Refinery", 20, 64),
        ("Spice Silo", 32, 62),
        ("Windtrap", 22, 56), ("Windtrap", 30, 66), ("Windtrap", 16, 62),
        ("Light Factory", 34, 58), ("Heavy Factory", 24, 68),
        ("Repair Yard", 32, 68), ("Outpost", 28, 62), ("Barracks", 18, 70),
        ("Police Station", 22, 64),
    ])
    zone_districts(sc, "Atreides", [
        District(12, 50, 30, 10, "IICR", fill=78, limit=14),
        District(12, 60, 30, 16, "RRCCI", fill=80, limit=22),
    ], rng)
    turret_ring(sc, "Atreides", [(12, 54), (38, 54), (40, 68), (16, 74)], rng)
    force(sc, "Atreides", 30, 72, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ])

    # Two unfinished outstations: bare rock with a windtrap and a road stub,
    # exactly the sites the player wants for silos three and four.
    street_path(sc, "Atreides", [(52, 28), (62, 32)], rng, 14)
    anchors(sc, "Atreides", [("Windtrap", 56, 30)],
            health=200, radius=10)
    street_path(sc, "Atreides", [(56, 94), (68, 98)], rng, 14)
    anchors(sc, "Atreides", [("Windtrap", 60, 96), ("Outpost", 66, 98)],
            health=200, radius=10)

    # -- Ordos --------------------------------------------------------------
    street_path(sc, "Ordos", [(94, 34), (106, 36), (116, 42)], rng, 18)
    street_path(sc, "Ordos", [(96, 46), (110, 48)], rng, 16)
    anchors(sc, "Ordos", [
        ("Const Yard", 102, 38), ("Refinery", 98, 34), ("Refinery", 110, 44),
        ("Heavy Factory", 106, 42), ("Windtrap", 96, 38), ("Windtrap", 112, 38),
        ("Repair Yard", 100, 46), ("Spice Silo", 114, 46), ("Outpost", 104, 34),
        ("Barracks", 94, 44),
    ])
    zone_districts(sc, "Ordos", [
        District(90, 28, 32, 24, "IIRCC", fill=76, limit=24),
    ], rng)
    turret_ring(sc, "Ordos", [(90, 32), (116, 32), (118, 50), (92, 50)], rng)
    force(sc, "Ordos", 104, 50, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Raider Trike", "Area Guard"),
        ("Quad", "Area Guard"), ("Launcher", "Area Guard"),
        ("Carryall", "Area Guard"),
    ])

    # -- Harkonnen ----------------------------------------------------------
    street_path(sc, "Harkonnen", [(92, 94), (102, 96), (110, 102)], rng, 18)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 98, 96), ("Refinery", 94, 92), ("Heavy Factory", 104, 100),
        ("Windtrap", 94, 100), ("Windtrap", 108, 96), ("Repair Yard", 100, 102),
        ("Outpost", 102, 92), ("Barracks", 92, 98),
    ])
    zone_districts(sc, "Harkonnen", [
        District(88, 88, 28, 20, "IIRC", fill=74, limit=18),
    ], rng)
    turret_ring(sc, "Harkonnen", [(88, 92), (112, 92), (112, 106), (90, 106)], rng)
    force(sc, "Harkonnen", 100, 106, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Launcher", "Area Guard"),
    ])

    sc.teams = [
        "Ordos,Normal,Wheeled,3,7",
        "Ordos,Guard,Foot,2,4",
        "Ordos,Staging,Tracked,3,6",
        "Harkonnen,Normal,Tracked,3,7",
        "Harkonnen,Guard,Foot,2,4",
    ]
    sc.reinforcements = [
        "Ordos,Raider Trike,Enemybase,9",
        "Harkonnen,Trike,Enemybase,9",
        "Ordos,Quad,Enemybase,18",
        "Harkonnen,Tank,Enemybase,18",
        "Ordos,Tank,Enemybase,28,+",
        "Harkonnen,Launcher,Enemybase,28,+",
        "Atreides,Harvester,Homebase,8",
        "Atreides,Harvester,Homebase,16",
        "Atreides,Carryall,Homebase,22",
        "Atreides,Quad,Homebase,26,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 6 -- Tuono Crossing: mobile expedition, no construction yard
# ===========================================================================
def scenario_tuono_crossing() -> Scenario:
    W, H = 160, 96
    rng = Rng(0x7404C)
    c = Canvas(W, H)

    # A thin starting ledge with a depot but no Const Yard, a broken land
    # bridge across the middle, and real ground at the far end.
    c.ribbon([(6, 40), (18, 44), (26, 48)], ROCK, 9, rng)
    c.ribbon([(30, 50), (44, 44), (56, 52), (70, 46)], ROCK, 6, rng)  # the bridge
    c.organic(96, 44, 18, ROCK, rng, lobes=9)                 # the destination
    c.organic(136, 26, 13, ROCK, rng, lobes=7)                # Sardaukar post
    c.organic(132, 72, 14, ROCK, rng, lobes=7)                # Sardaukar main
    c.organic(74, 76, 10, ROCK, rng, lobes=5)                 # Harkonnen block
    c.ribbon([(34, 10), (48, 22), (62, 16), (76, 26)], MOUNTAIN, 7, rng)
    c.ribbon([(34, 82), (50, 74), (64, 84)], MOUNTAIN, 7, rng)

    for (sx, sy, r, rich) in [(88, 22, 10, True), (104, 66, 11, True),
                              (118, 44, 9, True), (20, 20, 8, False),
                              (16, 74, 8, False), (150, 50, 9, True),
                              (66, 64, 7, False), (86, 88, 8, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(52, 26), (52, 70), (118, 86)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(82, 54, BLOOM)
    c.set(112, 20, BLOOM)

    sc = Scenario(
        filename="3P - 160x96 - Tuono Crossing.ini",
        size_x=W, size_y=H,
        header=[
            "; Tuono Crossing -- 160x96, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; You start as a column, not a city: a supply ledge with a depot,",
            "; a silo and an outpost, two mothballed MCVs and everything that",
            "; could still drive. There is no construction yard on the ledge",
            "; and no room for one. The Tuono land bridge is intact but narrow",
            "; and Harkonnen sit on the far anchorage. Cross, deploy, build a",
            "; real town on the eastern rock, then break the Sardaukar posts.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (bridgehead), Sardaukar (eastern posts).",
            "; Win     : clear both hostile houses. Lose: your buildings gone,",
            ";           so keep the depot alive until an MCV is down.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="MACHINE.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 8000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 13000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- the ledge: a depot, not a base -----------------------------------
    street_path(sc, "Atreides", [(8, 40), (18, 44), (26, 46)], rng, 14)
    anchors(sc, "Atreides", [
        ("Windtrap", 12, 42), ("Spice Silo", 16, 42), ("Outpost", 20, 46),
        ("Windtrap", 22, 42),
    ], radius=10)
    zone_districts(sc, "Atreides", [
        District(4, 36, 26, 14, "RCI", fill=55, limit=8),
    ], rng)
    turret_ring(sc, "Atreides", [(8, 38), (26, 50)], rng)
    force(sc, "Atreides", 18, 50, [
        ("MCV", "Area Guard"), ("MCV", "Area Guard"),
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Troopers", "Area Guard"),
        ("Troopers", "Area Guard"),
    ], spread=2)

    # -- Harkonnen hold the far anchorage ---------------------------------
    street_path(sc, "Harkonnen", [(66, 72), (78, 74), (84, 80)], rng, 16)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 74, 74), ("Refinery", 70, 70), ("Heavy Factory", 78, 78),
        ("Windtrap", 70, 78), ("Windtrap", 82, 72), ("Repair Yard", 74, 80),
        ("Outpost", 78, 70), ("Barracks", 68, 76),
    ])
    zone_districts(sc, "Harkonnen", [
        District(62, 64, 26, 20, "IIRC", fill=74, limit=16),
    ], rng)
    turret_ring(sc, "Harkonnen", [(64, 68), (84, 68), (84, 84), (66, 84),
                                  (72, 52), (76, 56)], rng)
    force(sc, "Harkonnen", 76, 84, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
    ])

    # -- Sardaukar posts ---------------------------------------------------
    street_path(sc, "Sardaukar", [(128, 20), (140, 24), (146, 32)], rng, 16)
    anchors(sc, "Sardaukar", [
        ("Const Yard", 136, 24), ("Refinery", 132, 20), ("Heavy Factory", 140, 28),
        ("Windtrap", 130, 28), ("Windtrap", 144, 22), ("Repair Yard", 136, 32),
        ("Outpost", 140, 20), ("Barracks", 128, 26), ("Police Station", 134, 28),
    ])
    zone_districts(sc, "Sardaukar", [
        District(124, 14, 30, 24, "IIRCC", fill=78, limit=22),
    ], rng)
    turret_ring(sc, "Sardaukar", [(124, 18), (148, 18), (150, 36), (126, 38)],
                rng, rocket_every=2)

    street_path(sc, "Sardaukar", [(122, 68), (136, 70), (146, 76)], rng, 16)
    street_path(sc, "Sardaukar", [(124, 80), (138, 82)], rng, 14)
    anchors(sc, "Sardaukar", [
        ("Const Yard", 132, 70), ("Refinery", 128, 66), ("Refinery", 140, 78),
        ("Heavy Factory", 136, 74), ("Hightech Factory", 126, 76),
        ("Windtrap", 124, 70), ("Windtrap", 142, 70), ("Windtrap", 132, 82),
        ("Starport", 144, 84), ("Outpost", 134, 66), ("House IX", 138, 86),
        ("WOR", 122, 80), ("Repair Yard", 130, 86),
    ])
    zone_districts(sc, "Sardaukar", [
        District(116, 60, 40, 30, "IIRCC", fill=80, limit=32),
    ], rng)
    turret_ring(sc, "Sardaukar", [(116, 64), (148, 62), (152, 84), (118, 88),
                                  (108, 44), (112, 50)], rng, rocket_every=2)
    force(sc, "Sardaukar", 134, 88, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Troopers", "Area Guard"), ("Quad", "Area Guard"),
    ])
    force(sc, "Sardaukar", 138, 34, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,5",
        "Harkonnen,Normal,Tracked,3,7",
        "Sardaukar,Guard,Foot,3,6",
        "Sardaukar,Normal,Tracked,4,8",
        "Sardaukar,Staging,Wheeled,3,6",
    ]
    sc.reinforcements = [
        "Harkonnen,Quad,Enemybase,10",
        "Harkonnen,Tank,Enemybase,19",
        "Sardaukar,Troopers,Homebase,14,+",
        "Sardaukar,Tank,Enemybase,24",
        "Sardaukar,Siege Tank,Enemybase,35",
        "Sardaukar,Launcher,Enemybase,45,+",
        # The expedition's own supply line: harvesters first, hull later.
        "Atreides,Harvester,Homebase,12",
        "Atreides,MCV,Homebase,26",
        "Atreides,Tank,Homebase,33,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 7 -- Carthag Vise: two-front siege
# ===========================================================================
def scenario_carthag_vise() -> Scenario:
    W = H = 128
    rng = Rng(0xCA871)
    c = Canvas(W, H)

    # A north-south valley floor with the player's city in the middle and an
    # enemy city pressing from each end.
    c.ribbon([(48, 14), (58, 40), (56, 64), (64, 90), (60, 114)], ROCK, 34, rng)
    c.ribbon([(22, 50), (40, 58), (54, 62)], ROCK, 12, rng)   # western annex
    c.ribbon([(72, 60), (92, 56), (104, 64)], ROCK, 12, rng)  # eastern annex
    c.ribbon([(28, 8), (34, 34), (26, 60), (32, 90), (24, 118)], MOUNTAIN, 8, rng)
    c.ribbon([(96, 8), (92, 32), (100, 58), (94, 90), (102, 118)], MOUNTAIN, 8, rng)
    c.organic(106, 22, 10, ROCK, rng, lobes=5)                # Sardaukar battery

    for (sx, sy, r, rich) in [(18, 30, 9, True), (20, 92, 9, True),
                              (114, 44, 9, True), (112, 100, 9, True),
                              (76, 30, 8, False), (44, 98, 8, False),
                              (84, 108, 7, False), (40, 16, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(14, 60), (118, 70), (78, 118)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(70, 46, BLOOM)
    c.set(46, 80, BLOOM)

    sc = Scenario(
        filename="4P - 128x128 - Carthag Vise.ini",
        size_x=W, size_y=H,
        header=[
            "; Carthag Vise -- 128x128, 4 houses, DuneCity city-sim scenario.",
            ";",
            "; Your city holds the middle of the valley and both ends belong to",
            "; somebody else. Harkonnen come down from the north, Ordos up from",
            "; the south, and a Sardaukar battery on the eastern shoulder ranges",
            "; on your eastern annex whenever it feels like it. The annexes are",
            "; where your spice and your second refinery live; the core is where",
            "; your people live. You cannot hold all three at full strength.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (north), Ordos (south), Sardaukar (battery).",
            "; Win     : clear all three. Lose: your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="BLOODY.WSA", winpic="WIN2.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 12000, "Quota": 0}),
            ("Ordos", {"Brain": "Team2", "Credits": 12000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 5000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- Atreides valley city ---------------------------------------------
    street_path(sc, "Atreides", [(44, 54), (58, 56), (72, 58)], rng, 18)
    street_path(sc, "Atreides", [(44, 66), (58, 68), (74, 66)], rng, 18)
    street_path(sc, "Atreides", [(56, 46), (57, 78)], rng, 12)
    street_path(sc, "Atreides", [(66, 48), (67, 76)], rng, 12)
    street_path(sc, "Atreides", [(30, 56), (44, 60)], rng, 18)   # west annex road
    street_path(sc, "Atreides", [(76, 60), (94, 60)], rng, 18)   # east annex road
    spurs(sc, "Atreides", [(44, 54), (72, 58), (44, 66), (57, 78)], rng, 7)

    anchors(sc, "Atreides", [
        ("Const Yard", 56, 60), ("Refinery", 60, 56), ("Heavy Factory", 52, 58),
        ("Hightech Factory", 62, 64), ("Light Factory", 48, 66),
        ("Windtrap", 50, 54), ("Windtrap", 66, 56), ("Windtrap", 52, 70),
        ("Windtrap", 68, 70), ("Spice Silo", 46, 62),
        ("Repair Yard", 64, 72), ("Outpost", 58, 66), ("House IX", 54, 66),
        ("Barracks", 70, 62), ("Police Station", 60, 70),
        ("Police Station", 50, 60),
    ])
    zone_districts(sc, "Atreides", [
        District(40, 44, 40, 14, "IIRCC", fill=80, limit=22),
        District(40, 58, 40, 12, "RRCCIP", fill=84, limit=28),
        District(40, 70, 40, 16, "RRRCI", fill=78, limit=22),
    ], rng)
    # West annex: refinery and housing, thin on defence.
    anchors(sc, "Atreides", [
        ("Refinery", 30, 56), ("Windtrap", 26, 54), ("Spice Silo", 34, 58),
        ("Outpost", 38, 56),
    ], radius=12)
    zone_districts(sc, "Atreides", [
        District(20, 48, 24, 16, "RRCI", fill=72, limit=12),
    ], rng)
    # East annex: the exposed one, under the Sardaukar guns.
    anchors(sc, "Atreides", [
        ("Refinery", 88, 58), ("Windtrap", 84, 62), ("Light Factory", 92, 62),
        ("Spice Silo", 80, 58),
    ], radius=12)
    zone_districts(sc, "Atreides", [
        District(74, 52, 30, 16, "RICC", fill=70, limit=12),
    ], rng)
    turret_ring(sc, "Atreides", [(42, 46), (74, 46), (78, 76), (42, 78),
                                 (96, 58), (24, 50)], rng)
    force(sc, "Atreides", 58, 76, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    # -- Harkonnen northern city -------------------------------------------
    street_path(sc, "Harkonnen", [(42, 18), (56, 20), (68, 24)], rng, 16)
    street_path(sc, "Harkonnen", [(44, 30), (60, 32), (70, 34)], rng, 16)
    street_path(sc, "Harkonnen", [(54, 14), (55, 38)], rng, 10)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 52, 22), ("Refinery", 48, 18), ("Refinery", 64, 30),
        ("Heavy Factory", 58, 24), ("Hightech Factory", 46, 28),
        ("Windtrap", 44, 22), ("Windtrap", 62, 20), ("Windtrap", 52, 34),
        ("Repair Yard", 66, 22), ("Outpost", 56, 30), ("Barracks", 42, 32),
        ("Starport", 68, 30), ("WOR", 40, 24),
    ])
    zone_districts(sc, "Harkonnen", [
        District(36, 12, 40, 14, "IIRCC", fill=80, limit=24),
        District(36, 26, 40, 16, "RRCCI", fill=80, limit=26),
    ], rng)
    turret_ring(sc, "Harkonnen", [(38, 16), (72, 16), (74, 36), (38, 38),
                                  (56, 42), (48, 42)], rng, rocket_every=2)
    force(sc, "Harkonnen", 58, 38, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Devastator", "Area Guard"), ("Quad", "Area Guard"),
    ])

    # -- Ordos southern city ------------------------------------------------
    street_path(sc, "Ordos", [(46, 96), (60, 98), (74, 100)], rng, 16)
    street_path(sc, "Ordos", [(48, 108), (62, 110), (72, 108)], rng, 16)
    street_path(sc, "Ordos", [(60, 92), (61, 114)], rng, 10)
    anchors(sc, "Ordos", [
        ("Const Yard", 58, 100), ("Refinery", 54, 96), ("Refinery", 68, 106),
        ("Heavy Factory", 64, 100), ("Hightech Factory", 50, 106),
        ("Windtrap", 50, 100), ("Windtrap", 70, 98), ("Windtrap", 58, 112),
        ("Repair Yard", 72, 102), ("Outpost", 62, 104), ("Barracks", 46, 102),
        ("Starport", 74, 110), ("WOR", 44, 110),
    ])
    zone_districts(sc, "Ordos", [
        District(40, 90, 40, 14, "IIRCC", fill=80, limit=24),
        District(40, 104, 40, 16, "RRCCI", fill=80, limit=26),
    ], rng)
    turret_ring(sc, "Ordos", [(42, 94), (78, 94), (78, 114), (42, 116),
                              (58, 88), (66, 88)], rng, rocket_every=2)
    force(sc, "Ordos", 60, 88, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Deviator", "Area Guard"), ("Raider Trike", "Area Guard"),
    ])

    # -- Sardaukar battery on the eastern shoulder --------------------------
    street_path(sc, "Sardaukar", [(100, 20), (110, 24)], rng, 14)
    anchors(sc, "Sardaukar", [
        ("Windtrap", 104, 20), ("Barracks", 108, 22), ("Outpost", 100, 24),
        ("Light Factory", 110, 26),
    ], radius=10)
    turret_ring(sc, "Sardaukar", [(100, 18), (112, 18), (112, 30), (100, 30)],
                rng, rocket_every=1)
    force(sc, "Sardaukar", 106, 30, [
        ("Launcher", "Area Guard"), ("Launcher", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Troopers", "Guard"), ("Troopers", "Guard"),
    ])

    sc.teams = [
        "Harkonnen,Normal,Tracked,4,9",
        "Harkonnen,Staging,Wheeled,3,6",
        "Harkonnen,Guard,Foot,2,5",
        "Ordos,Normal,Wheeled,4,9",
        "Ordos,Kamikaze,Tracked,3,6",
        "Ordos,Guard,Foot,2,5",
        "Sardaukar,Guard,Foot,2,4",
    ]
    # Deliberately offset so the two fronts rarely peak together -- until the
    # repeating waves from minute 30 onwards, which do.
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,6",
        "Ordos,Raider Trike,Enemybase,9",
        "Harkonnen,Tank,Enemybase,15",
        "Ordos,Tank,Enemybase,19",
        "Harkonnen,Siege Tank,Enemybase,24",
        "Ordos,Launcher,Enemybase,27",
        "Harkonnen,Tank,Enemybase,30,+",
        "Ordos,Siege Tank,Enemybase,30,+",
        "Sardaukar,Launcher,Homebase,21,+",
        "Atreides,Harvester,Homebase,10",
        "Atreides,Tank,Homebase,18",
        "Atreides,Launcher,Homebase,28,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 8 -- Arrakeen Blackout: broken power, urban recovery
# ===========================================================================
def scenario_arrakeen_blackout() -> Scenario:
    W = H = 128
    rng = Rng(0xB1AC0)
    c = Canvas(W, H)

    c.ribbon([(24, 34), (48, 30), (74, 40), (96, 34)], ROCK, 30, rng)
    c.ribbon([(36, 60), (58, 70), (84, 66)], ROCK, 26, rng)
    c.ribbon([(52, 48), (60, 56)], ROCK, 14, rng)             # the isthmus
    c.organic(96, 96, 14, ROCK, rng, lobes=7)                 # Harkonnen
    c.organic(26, 92, 12, ROCK, rng, lobes=6)                 # power district
    c.ribbon([(14, 62), (22, 78), (20, 92)], ROCK, 7, rng)    # road south

    for _ in range(14):
        c.organic(rng.between(30, 92), rng.between(28, 74),
                  rng.between(2, 3), SAND, rng, lobes=3, only_if=(ROCK,))

    for (sx, sy, r, rich) in [(110, 18, 9, True), (12, 18, 9, True),
                              (110, 62, 9, True), (60, 104, 9, True),
                              (46, 16, 8, False), (88, 112, 8, False),
                              (10, 44, 7, False), (118, 92, 8, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(66, 96), (108, 44), (34, 112)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(70, 22, BLOOM)
    c.set(44, 84, BLOOM)

    sc = Scenario(
        filename="3P - 128x128 - Arrakeen Blackout.ini",
        size_x=W, size_y=H,
        header=[
            "; Arrakeen Blackout -- 128x128, 3 houses, DuneCity city-sim map.",
            ";",
            "; The city is intact. The grid is not. A raid took the southern",
            "; power district and every windtrap in it; what is left on the",
            "; plateau is two damaged traps feeding a city that will draw many",
            "; times their output the moment the zones repopulate. Unpowered",
            "; zones score -500 and decline, so this is a race: restore supply",
            "; before the neighbourhoods empty out. Mercenaries are sitting on",
            "; the old power district and Harkonnen are watching the clock.",
            ";",
            "; This is the one map that hands you a large prebuilt city, and",
            "; it hands it to you broken on purpose.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Mercenary (old power district), Harkonnen (south-east).",
            "; Win     : clear both hostile houses. Lose: your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="MACHINE.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 13000, "Quota": 0}),
            ("Mercenary", {"Brain": "Team2", "Credits": 6000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 11000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- the blacked-out city ---------------------------------------------
    street_path(sc, "Atreides", [(24, 32), (48, 30), (72, 38), (94, 34)], rng, 16)
    street_path(sc, "Atreides", [(24, 42), (50, 40), (74, 48), (94, 44)], rng, 16)
    street_path(sc, "Atreides", [(40, 24), (42, 50)], rng, 10)
    street_path(sc, "Atreides", [(62, 24), (64, 52)], rng, 10)
    street_path(sc, "Atreides", [(84, 26), (86, 48)], rng, 10)
    street_path(sc, "Atreides", [(56, 50), (60, 62)], rng, 12)
    street_path(sc, "Atreides", [(38, 64), (60, 72), (82, 68)], rng, 16)
    street_path(sc, "Atreides", [(40, 74), (62, 80)], rng, 16)
    street_path(sc, "Atreides", [(20, 64), (20, 90)], rng, 12)
    spurs(sc, "Atreides", [(24, 32), (94, 34), (38, 64), (82, 68), (24, 42)],
          rng, 10)

    # Two damaged windtraps for a city that will want ten or more.
    anchors(sc, "Atreides", [
        ("Const Yard", 48, 34), ("Refinery", 44, 30), ("Refinery", 76, 44),
        ("Heavy Factory", 54, 36), ("Hightech Factory", 68, 32),
        ("Light Factory", 34, 38), ("Repair Yard", 88, 40),
        ("Spice Silo", 30, 34), ("Spice Silo", 90, 46),
        ("Outpost", 58, 42), ("House IX", 72, 46), ("Airport", 80, 34),
        ("Barracks", 36, 44), ("WOR", 86, 30), ("Police Station", 52, 44),
        ("Police Station", 78, 38), ("Starport", 64, 44),
    ], health=205)
    # Four traps for a city whose buildings alone draw six hundred, and whose
    # zones will want several times that again once they repopulate.
    anchors(sc, "Atreides", [("Windtrap", 46, 40), ("Windtrap", 70, 40)],
            health=120)
    anchors(sc, "Atreides", [("Windtrap", 34, 30), ("Windtrap", 84, 44)],
            health=256)
    zone_districts(sc, "Atreides", [
        District(20, 22, 80, 14, "IICRR", fill=84, limit=34),
        District(20, 36, 80, 14, "RRCCIP", fill=86, limit=40),
        District(32, 58, 56, 12, "RRCCI", fill=82, limit=28),
        District(32, 70, 56, 16, "RRRCI", fill=80, limit=26),
    ], rng)
    turret_ring(sc, "Atreides", [(22, 26), (54, 20), (92, 26), (98, 44),
                                 (84, 74), (36, 78), (18, 60)], rng)
    force(sc, "Atreides", 50, 50, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Launcher", "Area Guard"),
        ("Troopers", "Area Guard"), ("Trike", "Area Guard"),
    ])

    # -- Mercenaries squatting the old power district ----------------------
    # Four intact windtraps stand here, on the wrong side of the account.
    street_path(sc, "Mercenary", [(18, 88), (30, 90), (36, 96)], rng, 16)
    anchors(sc, "Mercenary", [
        ("Const Yard", 26, 90), ("Windtrap", 20, 88), ("Windtrap", 22, 94),
        ("Windtrap", 32, 88), ("Windtrap", 32, 94), ("Refinery", 28, 96),
        ("Heavy Factory", 34, 92), ("Outpost", 24, 86), ("Barracks", 18, 94),
    ])
    zone_districts(sc, "Mercenary", [
        District(12, 80, 32, 24, "IIRC", fill=74, limit=16),
    ], rng)
    turret_ring(sc, "Mercenary", [(14, 84), (38, 84), (38, 100), (16, 100)], rng)
    force(sc, "Mercenary", 30, 100, [
        ("Harvester", "Harvest"), ("Raider Trike", "Area Guard"),
        ("Quad", "Area Guard"), ("Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    # -- Harkonnen ----------------------------------------------------------
    street_path(sc, "Harkonnen", [(88, 90), (100, 92), (108, 98)], rng, 16)
    street_path(sc, "Harkonnen", [(90, 102), (104, 104)], rng, 14)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 96, 92), ("Refinery", 92, 88), ("Refinery", 104, 100),
        ("Heavy Factory", 100, 96), ("Windtrap", 92, 96), ("Windtrap", 106, 92),
        ("Repair Yard", 96, 102), ("Outpost", 100, 88), ("Barracks", 88, 98),
        ("Hightech Factory", 108, 104), ("Starport", 88, 104), ("WOR", 110, 92),
    ])
    zone_districts(sc, "Harkonnen", [
        District(84, 84, 32, 26, "IIRCC", fill=82, limit=36),
    ], rng)
    turret_ring(sc, "Harkonnen", [(84, 88), (110, 88), (112, 106), (86, 108)], rng)
    force(sc, "Harkonnen", 98, 108, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Devastator", "Area Guard"), ("Carryall", "Area Guard"),
    ])

    sc.teams = [
        "Mercenary,Guard,Foot,2,5",
        "Mercenary,Normal,Wheeled,3,6",
        "Harkonnen,Normal,Tracked,4,8",
        "Harkonnen,Staging,Tracked,3,6",
        "Harkonnen,Guard,Foot,2,4",
    ]
    sc.reinforcements = [
        "Mercenary,Raider Trike,Enemybase,8",
        "Mercenary,Quad,Enemybase,17,+",
        "Harkonnen,Trike,Enemybase,12",
        "Harkonnen,Tank,Enemybase,21",
        "Harkonnen,Siege Tank,Enemybase,31",
        "Harkonnen,Tank,Enemybase,40,+",
        "Atreides,Harvester,Homebase,13",
        "Atreides,Quad,Homebase,20",
        "Atreides,Tank,Homebase,32,+",
    ]
    # Deliberately no Atreides wind farm: the blackout is the mission.
    power_finish(sc, skip=("Atreides",))
    return sc


# ===========================================================================
# 9 -- Shield Wall Rift: three rival houses, each on its own team
# ===========================================================================
def scenario_shield_wall_rift() -> Scenario:
    W, H = 144, 112
    rng = Rng(0x5A1E1)
    c = Canvas(W, H)

    # Four quadrants around a central rift; everyone touches everyone.
    c.organic(28, 30, 15, ROCK, rng, lobes=8)                 # Atreides
    c.organic(116, 26, 17, ROCK, rng, lobes=9)                # Harkonnen
    c.organic(116, 86, 17, ROCK, rng, lobes=9)                # Ordos
    c.organic(32, 88, 16, ROCK, rng, lobes=8)                 # Fremen
    c.organic(72, 56, 13, ROCK, rng, lobes=7)                 # the contested rift
    c.ribbon([(60, 10), (68, 34), (64, 54)], MOUNTAIN, 7, rng)
    c.ribbon([(80, 60), (88, 84), (82, 104)], MOUNTAIN, 7, rng)
    c.ribbon([(48, 46), (60, 56), (48, 68)], ROCK, 6, rng)
    c.ribbon([(84, 46), (96, 56), (86, 68)], ROCK, 6, rng)

    for (sx, sy, r, rich) in [(70, 20, 10, True), (70, 94, 10, True),
                              (14, 58, 9, True), (132, 56, 9, True),
                              (48, 14, 8, False), (96, 16, 8, False),
                              (48, 100, 8, False), (100, 102, 8, False),
                              (74, 56, 6, True)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(52, 60), (96, 36), (24, 60)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(72, 42, BLOOM)
    c.set(72, 72, BLOOM)

    sc = Scenario(
        filename="4P - 144x112 - Shield Wall Rift.ini",
        size_x=W, size_y=H,
        header=[
            "; Shield Wall Rift -- 144x112, 4 houses, DuneCity city-sim map.",
            ";",
            "; Four corners, one rift, no alliances. Harkonnen, Ordos and the",
            "; Fremen each answer to themselves here (separate Brain teams), so",
            "; they will fight each other over the rift spice as readily as",
            "; they fight you. You are the smallest of the four. Let them grind,",
            "; pick the moment, and remember the win condition needs all three",
            "; gone -- including whichever one wins the middle.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (Team2), Ordos (Team3), Fremen (Team4).",
            "; Win     : clear all three. Lose: your buildings gone.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="SARDUKAR.WSA", winpic="WIN2.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 8000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 11000, "Quota": 0}),
            ("Ordos", {"Brain": "Team3", "Credits": 11000, "Quota": 0}),
            ("Fremen", {"Brain": "Team4", "Credits": 10000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    def quarter(owner: str, ox: int, oy: int, mix_limit: int, army,
                extra: Sequence[Tuple[str, int, int]] = (), police: bool = True):
        street_path(sc, owner, [(ox - 12, oy - 6), (ox, oy - 4), (ox + 12, oy - 6)],
                    rng, 18)
        street_path(sc, owner, [(ox - 12, oy + 6), (ox, oy + 8), (ox + 12, oy + 6)],
                    rng, 18)
        street_path(sc, owner, [(ox - 2, oy - 12), (ox - 1, oy + 12)], rng, 10)
        street_path(sc, owner, [(ox + 8, oy - 10), (ox + 9, oy + 10)], rng, 10)
        spurs(sc, owner, [(ox - 12, oy - 6), (ox + 12, oy - 6), (ox - 12, oy + 6)],
              rng, 6)
        core = [
            ("Const Yard", ox, oy), ("Refinery", ox - 4, oy - 4),
            ("Heavy Factory", ox + 4, oy - 2), ("Light Factory", ox - 6, oy + 4),
            ("Windtrap", ox - 2, oy - 6), ("Windtrap", ox + 6, oy + 4),
            ("Windtrap", ox - 8, oy), ("Spice Silo", ox + 8, oy - 6),
            ("Repair Yard", ox + 2, oy + 6), ("Outpost", ox + 2, oy + 2),
            ("Barracks", ox - 8, oy + 8),
        ]
        if police:
            core.append(("Police Station", ox - 4, oy + 2))
        anchors(sc, owner, core + list(extra))
        zone_districts(sc, owner, [
            District(ox - 16, oy - 14, 32, 12, "IIRCC", fill=80, limit=mix_limit // 2),
            District(ox - 16, oy - 2, 32, 16, "RRCCIP", fill=82, limit=mix_limit),
        ], rng)
        turret_ring(sc, owner, [(ox - 16, oy - 10), (ox + 14, oy - 10),
                                (ox + 16, oy + 10), (ox - 14, oy + 12)], rng)
        force(sc, owner, ox, oy + 12, army)

    quarter("Atreides", 28, 30, 20, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])
    quarter("Harkonnen", 116, 28, 26, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Devastator", "Area Guard"), ("Quad", "Area Guard"),
    ], extra=[("Starport", 124, 34), ("Hightech Factory", 110, 34)])
    quarter("Ordos", 116, 86, 26, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Deviator", "Area Guard"),
        ("Raider Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ], extra=[("Starport", 124, 92), ("Hightech Factory", 110, 92)])
    quarter("Fremen", 32, 88, 24, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Troopers", "Area Guard"),
        ("Troopers", "Area Guard"), ("Quad", "Area Guard"),
    ], extra=[("WOR", 24, 94)], police=False)

    # A small unclaimed prize in the rift: nobody owns it at t=0.
    street_path(sc, "Atreides", [(66, 54), (78, 58)], rng, 16)

    sc.teams = [
        "Harkonnen,Normal,Tracked,4,9",
        "Harkonnen,Staging,Wheeled,3,6",
        "Harkonnen,Guard,Foot,2,5",
        "Ordos,Normal,Wheeled,4,9",
        "Ordos,Kamikaze,Tracked,3,6",
        "Ordos,Guard,Foot,2,5",
        "Fremen,Normal,Tracked,3,8",
        "Fremen,Guard,Foot,3,6",
    ]
    sc.reinforcements = [
        "Harkonnen,Tank,Homebase,14",
        "Ordos,Tank,Homebase,14",
        "Fremen,Troopers,Homebase,14",
        "Harkonnen,Siege Tank,Homebase,26,+",
        "Ordos,Launcher,Homebase,26,+",
        "Fremen,Tank,Homebase,26,+",
        "Atreides,Harvester,Homebase,10",
        "Atreides,Tank,Homebase,22",
        "Atreides,Launcher,Homebase,34,+",
    ]
    power_finish(sc)
    return sc


# ===========================================================================
# 10 -- Harg Pass Convoy: five scattered supply towns
# ===========================================================================
def scenario_harg_pass_convoy() -> Scenario:
    W, H = 160, 128
    rng = Rng(0x4A26C)
    c = Canvas(W, H)

    # A chain of small rock pockets down a canyon system. No single big base.
    towns = [(20, 24), (52, 40), (82, 26), (96, 70), (60, 96)]
    for (tx, ty) in towns:
        c.organic(tx, ty, 10, ROCK, rng, lobes=6)
    c.ribbon([(20, 24), (36, 34), (52, 40)], ROCK, 5, rng)
    c.ribbon([(52, 40), (68, 32), (82, 26)], ROCK, 5, rng)
    c.ribbon([(82, 26), (92, 48), (96, 70)], ROCK, 5, rng)
    c.ribbon([(96, 70), (78, 84), (60, 96)], ROCK, 5, rng)
    c.ribbon([(26, 8), (48, 14), (72, 8), (96, 14)], MOUNTAIN, 7, rng)
    c.ribbon([(28, 56), (44, 66), (36, 84)], MOUNTAIN, 8, rng)
    c.ribbon([(112, 40), (124, 60), (116, 84)], MOUNTAIN, 8, rng)

    c.organic(136, 30, 13, ROCK, rng, lobes=7)                # Harkonnen main
    c.organic(140, 100, 12, ROCK, rng, lobes=6)               # Harkonnen south
    c.organic(24, 104, 11, ROCK, rng, lobes=6)                # Mercenary bandits

    for (sx, sy, r, rich) in [(38, 18, 9, True), (70, 52, 10, True),
                              (110, 22, 9, True), (112, 104, 9, True),
                              (34, 118, 9, True), (14, 66, 8, False),
                              (150, 62, 9, True), (84, 116, 8, False),
                              (128, 14, 7, False), (64, 70, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(60, 60), (120, 90), (100, 108)]:
        c.organic(dx, dy, 6, DUNES, rng, lobes=4, only_if=(SAND,))
    c.set(48, 54, BLOOM)
    c.set(104, 44, BLOOM)

    sc = Scenario(
        filename="3P - 160x128 - Harg Pass Convoy.ini",
        size_x=W, size_y=H,
        header=[
            "; Harg Pass Convoy -- 160x128, 3 houses, DuneCity city-sim map.",
            ";",
            "; The Harg chain is five small supply towns strung down a canyon,",
            "; not one city. Only the first has a construction yard; the rest",
            "; have a windtrap, a refinery and whatever housing the crews put",
            "; up. Each town is individually weak and the raiders know it --",
            "; Harkonnen columns come down the eastern passes, Mercenary bandits",
            "; work the southern end. You will not hold all five at once.",
            "; Decide which links matter and let the rest bleed.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (two eastern bases), Mercenary (south-west).",
            "; Win     : clear both hostile houses. Lose: all your buildings",
            ";           gone -- losing individual towns is survivable.",
            "; Generated by scripts/gen_city_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="ATTACK.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 8500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 13000, "Quota": 0}),
            ("Mercenary", {"Brain": "Team2", "Credits": 7000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    def hamlet(ox: int, oy: int, core: Sequence[Tuple[str, int, int]],
               limit: int, mix: str, fill: int, army):
        street_path(sc, "Atreides", [(ox - 8, oy - 2), (ox, oy - 1), (ox + 8, oy - 3)],
                    rng, 22)
        street_path(sc, "Atreides", [(ox - 6, oy + 5), (ox + 6, oy + 4)], rng, 22)
        street_path(sc, "Atreides", [(ox, oy - 7), (ox + 1, oy + 7)], rng, 14)
        spurs(sc, "Atreides", [(ox - 8, oy - 2), (ox + 8, oy - 3)], rng, 3, (3, 6))
        anchors(sc, "Atreides", core)
        zone_districts(sc, "Atreides", [
            District(ox - 11, oy - 10, 22, 20, mix, fill=fill, limit=limit),
        ], rng)
        force(sc, "Atreides", ox, oy + 8, army)

    hamlet(20, 24, [
        ("Const Yard", 20, 24), ("Refinery", 16, 20), ("Windtrap", 24, 20),
        ("Light Factory", 24, 26), ("Spice Silo", 14, 26), ("Outpost", 18, 28),
        ("Heavy Factory", 22, 30), ("Police Station", 16, 24),
    ], 18, "RRCCI", 80, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Launcher", "Area Guard"),
    ])
    turret_ring(sc, "Atreides", [(12, 18), (28, 18), (28, 32), (12, 30)], rng)

    hamlet(52, 40, [
        ("Refinery", 50, 36), ("Windtrap", 56, 38), ("Spice Silo", 46, 42),
        ("Outpost", 54, 44), ("Barracks", 48, 46),
    ], 12, "RRCI", 72, [
        ("Harvester", "Harvest"), ("Quad", "Area Guard"), ("Trike", "Area Guard"),
    ])
    turret_ring(sc, "Atreides", [(44, 34), (60, 44)], rng)

    hamlet(82, 26, [
        ("Refinery", 80, 22), ("Windtrap", 86, 24), ("Repair Yard", 84, 30),
        ("Outpost", 78, 30),
    ], 11, "RRCI", 70, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Quad", "Area Guard"),
    ])
    turret_ring(sc, "Atreides", [(74, 20), (90, 32)], rng)

    hamlet(96, 70, [
        ("Refinery", 94, 66), ("Windtrap", 100, 68), ("Spice Silo", 92, 74),
        ("Light Factory", 100, 74),
    ], 11, "RRIC", 70, [
        ("Harvester", "Harvest"), ("Quad", "Area Guard"),
        ("Launcher", "Area Guard"),
    ])
    turret_ring(sc, "Atreides", [(88, 64), (104, 76)], rng)

    hamlet(60, 96, [
        ("Refinery", 58, 92), ("Windtrap", 64, 94), ("Barracks", 56, 100),
        ("Outpost", 64, 100),
    ], 11, "RRRC", 72, [
        ("Harvester", "Harvest"), ("Trike", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])
    turret_ring(sc, "Atreides", [(52, 90), (68, 102)], rng)

    # -- Harkonnen northern base ------------------------------------------
    street_path(sc, "Harkonnen", [(126, 24), (138, 26), (148, 32)], rng, 16)
    street_path(sc, "Harkonnen", [(128, 36), (142, 38)], rng, 14)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 134, 28), ("Refinery", 130, 24), ("Refinery", 142, 34),
        ("Heavy Factory", 138, 30), ("Hightech Factory", 128, 32),
        ("Windtrap", 126, 28), ("Windtrap", 144, 28), ("Windtrap", 134, 38),
        ("Repair Yard", 146, 38), ("Outpost", 136, 34), ("Barracks", 124, 34),
        ("Starport", 148, 24),
    ])
    zone_districts(sc, "Harkonnen", [
        District(120, 16, 36, 30, "IIRCC", fill=80, limit=28),
    ], rng)
    turret_ring(sc, "Harkonnen", [(120, 20), (152, 20), (154, 42), (122, 44)],
                rng, rocket_every=2)
    force(sc, "Harkonnen", 138, 44, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Devastator", "Area Guard"),
    ])

    # -- Harkonnen southern base -------------------------------------------
    street_path(sc, "Harkonnen", [(132, 96), (144, 98), (150, 104)], rng, 16)
    anchors(sc, "Harkonnen", [
        ("Const Yard", 140, 98), ("Refinery", 136, 94), ("Heavy Factory", 144, 102),
        ("Windtrap", 134, 100), ("Windtrap", 148, 96), ("Repair Yard", 140, 104),
        ("Outpost", 144, 94), ("Barracks", 132, 102),
    ])
    zone_districts(sc, "Harkonnen", [
        District(126, 88, 32, 26, "IIRC", fill=76, limit=18),
    ], rng)
    turret_ring(sc, "Harkonnen", [(126, 92), (154, 92), (154, 110), (128, 112)], rng)
    force(sc, "Harkonnen", 142, 110, [
        ("Harvester", "Harvest"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"),
    ])

    # -- Mercenary bandits --------------------------------------------------
    street_path(sc, "Mercenary", [(16, 100), (28, 102), (34, 108)], rng, 16)
    anchors(sc, "Mercenary", [
        ("Const Yard", 24, 102), ("Refinery", 20, 98), ("Light Factory", 28, 106),
        ("Windtrap", 18, 104), ("Windtrap", 30, 100), ("Outpost", 26, 98),
        ("Barracks", 16, 106),
    ])
    zone_districts(sc, "Mercenary", [
        District(12, 94, 28, 22, "IIRC", fill=72, limit=14),
    ], rng)
    turret_ring(sc, "Mercenary", [(12, 96), (36, 96), (36, 112), (14, 112)], rng)
    force(sc, "Mercenary", 26, 112, [
        ("Harvester", "Harvest"), ("Raider Trike", "Area Guard"),
        ("Raider Trike", "Area Guard"), ("Quad", "Area Guard"),
        ("Launcher", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Normal,Tracked,4,9",
        "Harkonnen,Staging,Wheeled,3,7",
        "Harkonnen,Kamikaze,Tracked,3,6",
        "Harkonnen,Guard,Foot,2,5",
        "Mercenary,Normal,Wheeled,3,7",
        "Mercenary,Guard,Foot,2,4",
    ]
    # Raids alternate ends of the chain so the player keeps having to choose.
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,7",
        "Mercenary,Raider Trike,Enemybase,11",
        "Harkonnen,Quad,Enemybase,16",
        "Mercenary,Quad,Enemybase,21",
        "Harkonnen,Tank,Enemybase,25",
        "Harkonnen,Siege Tank,Enemybase,34,+",
        "Mercenary,Troopers,Enemybase,29,+",
        "Atreides,Harvester,Homebase,9",
        "Atreides,Quad,Homebase,15",
        "Atreides,Tank,Homebase,23",
        "Atreides,Launcher,Homebase,36,+",
    ]
    power_finish(sc)
    return sc


# ---------------------------------------------------------------------------
# Internal legality assertions (not tidiness checks)
# ---------------------------------------------------------------------------
def check(sc: Scenario) -> Dict[str, int]:
    W, H = sc.size_x, sc.size_y
    owners = {name for name, _ in sc.houses}
    assert owners <= HOUSES, f"{sc.filename}: unknown house {owners - HOUSES}"

    occupied: Dict[Tuple[int, int], str] = {}
    for s in sc.structures:
        assert s.name in FOOTPRINT, f"{sc.filename}: unknown structure {s.name}"
        assert s.owner in owners, f"{sc.filename}: {s.name} owned by {s.owner}"
        assert 0 <= s.health <= 256, f"{sc.filename}: bad health {s.health}"
        w, h = FOOTPRINT[s.name]
        for dy in range(h):
            for dx in range(w):
                t = (s.x + dx, s.y + dy)
                assert 0 <= t[0] < W and 0 <= t[1] < H, \
                    f"{sc.filename}: {s.name} at {s.x},{s.y} out of bounds"
                assert t not in occupied, \
                    f"{sc.filename}: {s.name} at {s.x},{s.y} overlaps {occupied[t]}"
                assert sc.canvas.get(*t) == ROCK, \
                    f"{sc.filename}: {s.name} at {s.x},{s.y} not on rock"
                occupied[t] = f"{s.owner} {s.name}"

    roadseen: Set[Tuple[int, int]] = set()
    for owner, x, y in sc.roads:
        assert owner in owners, f"{sc.filename}: road owned by {owner}"
        assert 0 <= x < W and 0 <= y < H, f"{sc.filename}: road out of bounds"
        assert (x, y) not in occupied, f"{sc.filename}: road under {occupied[(x, y)]}"
        assert (x, y) not in roadseen, f"{sc.filename}: duplicate road {x},{y}"
        assert sc.canvas.get(x, y) == ROCK, f"{sc.filename}: road {x},{y} off rock"
        roadseen.add((x, y))

    unitseen: Set[Tuple[int, int]] = set()
    for u in sc.units:
        assert u.name in UNIT_NAMES, f"{sc.filename}: unknown unit {u.name}"
        assert u.mode in ATTACK_MODES, f"{sc.filename}: bad attack mode {u.mode}"
        assert u.owner in owners, f"{sc.filename}: unit owned by {u.owner}"
        assert 0 <= u.x < W and 0 <= u.y < H, f"{sc.filename}: unit out of bounds"
        assert (u.x, u.y) not in occupied, \
            f"{sc.filename}: unit inside {occupied[(u.x, u.y)]}"
        assert (u.x, u.y) not in unitseen, \
            f"{sc.filename}: two units on {u.x},{u.y}"
        assert sc.canvas.get(u.x, u.y) in DRIVEABLE, \
            f"{sc.filename}: unit on impassable {sc.canvas.get(u.x, u.y)}"
        unitseen.add((u.x, u.y))

    for t in sc.teams:
        parts = t.split(',')
        assert len(parts) == 5, f"{sc.filename}: bad team '{t}'"
        assert parts[0] in owners, f"{sc.filename}: team house {parts[0]}"
        assert parts[1] in TEAM_BEHAVIOURS, f"{sc.filename}: team behaviour {parts[1]}"
        assert parts[2] in TEAM_TYPES, f"{sc.filename}: team type {parts[2]}"
        assert int(parts[3]) <= int(parts[4]), f"{sc.filename}: team bounds '{t}'"

    for r in sc.reinforcements:
        parts = r.split(',')
        assert len(parts) in (4, 5), f"{sc.filename}: bad reinforcement '{r}'"
        assert parts[0] in owners, f"{sc.filename}: reinforcement house {parts[0]}"
        assert parts[1] in UNIT_NAMES, f"{sc.filename}: reinforcement unit {parts[1]}"
        assert parts[2] in DROP_LOCATIONS, f"{sc.filename}: drop location {parts[2]}"
        assert int(parts[3].rstrip('+')) >= 0, f"{sc.filename}: drop time '{r}'"
        if len(parts) == 5:
            assert parts[4] == '+', f"{sc.filename}: bad repeat flag '{r}'"

    # Objective sanity: a quota mission must require achievable storage
    # expansion, not begin solved.
    win = int(sc.basic["WinFlags"])
    if win & 8:
        assert int(sc.basic["TimeOut"]) > 0, f"{sc.filename}: timeout win, no TimeOut"
    quota = 0
    for name, body in sc.houses:
        if name == "Atreides":
            quota = int(body.get("Quota", 0))
    if win & 4:
        assert quota > 0, f"{sc.filename}: quota win flag with no quota"
        capacity = sum(10000 for s in sc.structures
                       if s.owner == "Atreides" and s.name == "Spice Silo")
        capacity += sum(1005 for s in sc.structures
                        if s.owner == "Atreides" and s.name == "Refinery")
        assert 0 < capacity < quota <= capacity + 20000, \
            f"{sc.filename}: quota should require one or two additional silos"
    else:
        assert quota == 0, f"{sc.filename}: quota set without the quota win flag"

    # Every house declared in [BASIC] order must actually own something.
    for name, _ in sc.houses:
        assert any(s.owner == name for s in sc.structures), \
            f"{sc.filename}: house {name} has no structures"

    # The human must be able to build: a construction yard or an MCV.
    has_yard = any(s.owner == "Atreides" and s.name == "Const Yard"
                   for s in sc.structures)
    has_mcv = any(u.owner == "Atreides" and u.name == "MCV" for u in sc.units)
    assert has_yard or has_mcv, f"{sc.filename}: Atreides can never build"

    # Power: nobody may start in brownout (zones are vacant at cycle zero, so
    # only the buildings draw), except the map whose premise is the blackout.
    blackout = "Blackout" in sc.filename
    ratios = []
    for name, _ in sc.houses:
        produced, standing = power_ledger(sc, name, level=1)
        standing -= sum(ZONE_POWER[s.name][0] for s in sc.structures
                        if s.owner == name and s.name in ZONE_POWER)
        grown = power_ledger(sc, name, level=2)[1]
        if not (blackout and name == "Atreides"):
            assert produced >= standing, (
                f"{sc.filename}: {name} starts in brownout "
                f"({produced} produced vs {standing} standing draw)")
        ratios.append(f"{name[:3]} {produced}/{grown}")

    zones = sum(1 for s in sc.structures if s.name in
                ("Residential Zone", "Commercial Zone", "Industrial Zone"))
    return {
        "structures": len(sc.structures),
        "zones": zones,
        "roads": len(sc.roads),
        "units": len(sc.units),
        "power": " ".join(ratios),
    }


def check_text(sc: Scenario, text: str) -> None:
    """Re-read the rendered INI the way the loader will: plain UTF-8, one
    section per header, unique keys inside a section, full-width map rows."""
    text.encode("utf-8").decode("utf-8")
    assert "\t" not in text, f"{sc.filename}: tab in INI output"
    section: Optional[str] = None
    keys: Dict[str, Set[str]] = {}
    maprows = 0
    for n, line in enumerate(text.split("\n"), start=1):
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            assert section not in keys, \
                f"{sc.filename}: duplicate section [{section}]"
            keys[section] = set()
            continue
        assert "=" in line, f"{sc.filename}:{n}: not a key/value line: {line!r}"
        assert section is not None, f"{sc.filename}:{n}: key outside a section"
        key, value = line.split("=", 1)
        assert key not in keys[section], \
            f"{sc.filename}: duplicate key {section}/{key}"
        keys[section].add(key)
        if section == "MAP" and key.isdigit():
            assert len(value) == sc.size_x, \
                f"{sc.filename}: map row {key} is {len(value)} wide"
            assert set(value) <= set(SAND + DUNES + SPICE + THICK + ROCK
                                     + MOUNTAIN + BLOOM), \
                f"{sc.filename}: bad terrain character in row {key}"
            assert int(key) == maprows, f"{sc.filename}: map row {key} out of order"
            maprows += 1
    assert maprows == sc.size_y, \
        f"{sc.filename}: {maprows} map rows, expected {sc.size_y}"
    for required in ("BASIC", "MAP", "STRUCTURES", "UNITS"):
        assert required in keys, f"{sc.filename}: missing [{required}]"
    for name, _ in sc.houses:
        assert name in keys, f"{sc.filename}: missing [{name}]"


FACTORIES: List[Callable[[], Scenario]] = [
    scenario_sihaya_basin,
    scenario_ash_quarter,
    scenario_coriolis_gap,
    scenario_cielago_watch,
    scenario_hagal_flats,
    scenario_tuono_crossing,
    scenario_carthag_vise,
    scenario_arrakeen_blackout,
    scenario_shield_wall_rift,
    scenario_harg_pass_convoy,
]


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for factory in FACTORIES:
        sc = factory()
        stats = check(sc)
        text = sc.render()
        check_text(sc, text)
        path = OUT_DIR / sc.filename
        path.write_text(text, encoding="utf-8")
        print(f"{sc.filename}: {stats['structures']} structures "
              f"({stats['zones']} zones), {stats['roads']} roads, "
              f"{stats['units']} units, {len(sc.reinforcements)} reinforcements, "
              f"{path.stat().st_size} bytes | power {stats['power']}")


if __name__ == "__main__":
    main()
