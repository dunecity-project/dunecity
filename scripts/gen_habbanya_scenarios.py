#!/usr/bin/env python3
"""Deterministic generator for the two Habbanya single-player city scenarios.

Both missions are stand-alone Atreides-human maps written into
``data/maps/singleplayer/``:

  2P - 128x128 - Habbanya Crescent.ini   small township + spare MCV, the
                                         Harkonnen city sits at the far pole
  3P - 128x128 - Habbanya Narrows.ini    mobile colony with a depot, two
                                         hostile outposts hold the two passes

Both are *original* terrain, but the geography is deliberately modelled on
Rippsblack's multiplayer map ``2P - 128x128 - 1v1 - Habbanya-Penny.ini``
(data/maps/multiplayer, CC-BY-SA).  The features that map is built from, and
that these two reproduce with their own shapes and seeds, are:

  * a giant oval mountain rim enclosing the whole playable interior, with an
    outer sea of thick spice beyond it and only a handful of carved passes
    through the wall;
  * two C-shaped mountain enclosures inside that oval, each wrapped around an
    interior spice basin and each opening toward the middle of the map;
  * broad winding sand lanes threading between the crescents -- the battle
    routes -- rather than an open bowl;
  * opposed main rock shelves at the north and south poles, with smaller
    second shelves within reach of each of them for expansion;
  * abundant spice and blooms, and worm danger on the exposed sand.

Engine facts these maps are built against (same sources as
``gen_city_scenarios.py``, which supplies every layout helper used here):

  * Zones are 2x2 and every structure sits on rock; roads are a tile flag and
    only stick to rock (CityConstants.h isCityBuildableTerrain).
  * City roles come from CityEffects.h getStructureCityRole: Const Yard,
    Refinery, Silo, Light/Heavy/HighTech Factory, Repair Yard and Starport are
    INDUSTRIAL; Outpost, House IX and Airport are COMMERCIAL; Barracks, WOR
    and Palace are RESIDENTIAL.  Both cities carry all three so a city seeder
    classifies them as DuneCity towns rather than bare Dune bases.
  * Sandworms are owned by ``Fremen`` with no ``[Fremen]`` section, exactly as
    Habbanya-Penny ships them.  INIMapLoader::getHouseID misses the
    housename2house map, falls back to getHouseByName("fremen") and
    getOrCreateHouse builds a neutral house for it (INIMapLoader.cpp:1143 and
    :1069), so no enemy slot is created; House::isAlive (House.h:64) subtracts
    Unit_Sandworm, so a worm-only house never blocks the
    WINLOSEFLAGS_AI_NO_BUILDINGS victory test in House.cpp:947.  Set
    ``INCLUDE_SANDWORMS = False`` below to ship both maps without worms.
  * Objectives are plain conquest, WinFlags=3 / LoseFlags=1.  No quota.

Rock is deliberately left under-built: both cities are compact and irregular,
and every shelf keeps open rock so the player has somewhere to zone.

Run:  python3 scripts/gen_habbanya_scenarios.py
      python3 scripts/gen_habbanya_scenarios.py --dump   (ASCII terrain proof)
"""

from __future__ import annotations

import sys
from collections import deque
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen_city_scenarios import (  # noqa: E402
    BLOOM, DRIVEABLE, DUNES, MOUNTAIN, OUT_DIR, ROCK, SAND, SPICE, THICK,
    Canvas, District, Rng, Scenario,
    DEFAULT_CHOAM, anchors, basic, check, check_text, force, power_finish,
    spurs, street_path, turret_ring, zone_districts,
)

# Worms are the one piece of this map family that needs a house the mission
# never declares.  Flip to False to regenerate both maps without them.
INCLUDE_SANDWORMS = True

SANDY = (SAND, DUNES, SPICE, THICK)
# Blooms are sand that detonates under the first vehicle to touch it, so they
# are passable ground for the route check even though nothing may spawn on one.
PASSABLE = tuple(DRIVEABLE) + (BLOOM,)


# ---------------------------------------------------------------------------
# Integer ellipse geometry -- the oval rim and the two crescents
# ---------------------------------------------------------------------------
_RING_STEPS = 64


def _norm(dx: int, dy: int, a: int, b: int) -> int:
    """Ellipse radius^2 scaled so that exactly 10000 lies on the curve."""
    return (dx * dx * 10000) // (a * a) + (dy * dy * 10000) // (b * b)


def _angle_index(dx: int, dy: int, n: int = _RING_STEPS) -> int:
    """Diamond angle, integer only: 0 = east, n/4 = south, n/2 = west.

    Used to modulate the ring radius smoothly all the way round.  A per-tile
    random wobble would punch one-tile holes through a mountain wall and quietly
    turn a barrier into a passage, so the noise has to be a function of the
    angle and nothing else.
    """
    den = abs(dx) + abs(dy)
    if den == 0:
        return 0
    if dx >= 0 and dy >= 0:
        q, f = 0, (dy * n) // (4 * den)
    elif dx < 0 and dy >= 0:
        q, f = 1, (-dx * n) // (4 * den)
    elif dx < 0 and dy < 0:
        q, f = 2, (-dy * n) // (4 * den)
    else:
        q, f = 3, (dx * n) // (4 * den)
    return (q * (n // 4) + f) % n


def _ripple(rng: Rng, amp: int, smooth: int = 3) -> List[int]:
    """A smooth closed noise loop over the ring angles."""
    v = [rng.between(-amp, amp) for _ in range(_RING_STEPS)]
    for _ in range(smooth):
        v = [(v[(i - 1) % _RING_STEPS] + 2 * v[i] + v[(i + 1) % _RING_STEPS]) // 4
             for i in range(_RING_STEPS)]
    return v


def ellipse_band(c: Canvas, cx: int, cy: int, a: int, b: int, thickness: int,
                 ch: str, rng: Rng, rough: int = 4,
                 only_if: Optional[Sequence[str]] = None,
                 keep: Optional[Callable[[int], bool]] = None) -> None:
    """Paint a wobbling elliptical wall.

    `keep(angle_index)` returning False leaves that arc unpainted, which is how
    a closed ring becomes a C.  The outer and inner edges share one offset
    ripple so the wall keeps its thickness everywhere.
    """
    off = _ripple(rng, rough)
    thk = _ripple(rng, 2)
    span = max(a, b) + rough + 2
    for y in range(cy - span, cy + span + 1):
        for x in range(cx - span, cx + span + 1):
            if not c.inside(x, y):
                continue
            dx, dy = x - cx, y - cy
            i = _angle_index(dx, dy)
            if keep is not None and not keep(i):
                continue
            ao, bo = max(3, a + off[i]), max(3, b + off[i])
            if _norm(dx, dy, ao, bo) > 10000:
                continue
            t = max(3, thickness + thk[i])
            ai, bi = max(2, ao - t), max(2, bo - t)
            if _norm(dx, dy, ai, bi) <= 10000:
                continue
            if only_if is not None and c.get(x, y) not in only_if:
                continue
            c.set(x, y, ch)


def ellipse_outside(c: Canvas, cx: int, cy: int, a: int, b: int, ch: str,
                    beyond: int = 10000,
                    only_if: Optional[Sequence[str]] = None) -> None:
    """Paint everything outside the given ellipse -- the outer spice sea."""
    for y in range(c.h):
        for x in range(c.w):
            if _norm(x - cx, y - cy, a, b) <= beyond:
                continue
            if only_if is not None and c.get(x, y) not in only_if:
                continue
            c.set(x, y, ch)


def arc_gap(centre: int, half: int) -> Callable[[int], bool]:
    """keep() that leaves a `2*half` wide arc of the ring unpainted."""
    def keep(i: int) -> bool:
        d = (i - centre) % _RING_STEPS
        if d > _RING_STEPS // 2:
            d -= _RING_STEPS
        return abs(d) > half
    return keep


def facing(cx: int, cy: int, tx: int, ty: int) -> int:
    """Ring angle index of (tx,ty) seen from (cx,cy)."""
    return _angle_index(tx - cx, ty - cy)


def bloom(c: Canvas, x: int, y: int) -> None:
    """A bloom only makes sense standing on open sand, never on rock."""
    if c.get(x, y) in SANDY:
        c.set(x, y, BLOOM)


# ---------------------------------------------------------------------------
# Terrain shell shared by both missions
# ---------------------------------------------------------------------------
def habbanya_shell(c: Canvas, rng: Rng, rim: Tuple[int, int, int, int, int],
                   passes: Sequence[Sequence[Tuple[int, int]]],
                   crescents: Sequence[Tuple[int, int, int, int, int, int, int]],
                   ) -> None:
    """The Habbanya-Penny skeleton: outer spice sea, oval rim, two crescents.

    `rim` is (cx, cy, a, b, thickness); `passes` are polylines carved through
    the wall; each crescent is (cx, cy, a, b, thickness, mouth_x, mouth_y) and
    opens toward the given mouth point.
    """
    rcx, rcy, ra, rb, rt = rim

    # The sea of spice beyond the wall: this is where the harvesters have to go
    # once the interior fields thin out, and it is worm country.
    ellipse_outside(c, rcx, rcy, ra + 2, rb + 2, SPICE, only_if=(SAND,))
    ellipse_outside(c, rcx, rcy, ra + 9, rb + 9, THICK, beyond=10000,
                    only_if=(SPICE,))
    ellipse_band(c, rcx, rcy, ra, rb, rt, MOUNTAIN, rng, rough=4)

    # Carved passes: the only ground routes between the bowl and the outer sea.
    for line in passes:
        c.ribbon(line, SAND, 11, rng, only_if=(MOUNTAIN,))

    # The twin crescents, each cupping its own spice basin.
    for (ccx, ccy, ca, cb, ct, mx, my) in crescents:
        ellipse_band(c, ccx, ccy, ca, cb, ct, MOUNTAIN, rng, rough=3,
                     keep=arc_gap(facing(ccx, ccy, mx, my), 10))
        c.spice_field(ccx, ccy, max(4, min(ca, cb) - 5), rng, rich=True)
        bloom(c, ccx, ccy)


def lanes(c: Canvas, rng: Rng,
          lines: Sequence[Tuple[Sequence[Tuple[int, int]], int]]) -> None:
    """Dune-floored battle lanes.

    Painted with ``only_if`` sand so a lane can never cut a hole through a
    mountain wall or erase a shelf; where a lane meets rock it simply stops,
    which is what makes the crescents define the routes.
    """
    for pts, width in lines:
        c.ribbon(pts, DUNES, width, rng, only_if=(SAND,))


def shelves(c: Canvas, rng: Rng,
            plates: Sequence[Tuple[int, int, int, int]]) -> None:
    """Rock shelves. ``only_if`` sandy so shelves never eat the mountain walls;
    a shelf that runs into the rim is clipped by it, which is the point."""
    for (x, y, r, lobes) in plates:
        c.organic(x, y, r, ROCK, rng, lobes=lobes, only_if=SANDY)


def aprons(c: Canvas, rng: Rng,
           ribbons: Sequence[Tuple[Sequence[Tuple[int, int]], int]]) -> None:
    for pts, width in ribbons:
        c.ribbon(pts, ROCK, width, rng, only_if=SANDY)


def spice(c: Canvas, rng: Rng,
          fields: Sequence[Tuple[int, int, int, bool]]) -> None:
    for (x, y, r, rich) in fields:
        c.spice_field(x, y, r, rng, rich)


# ---------------------------------------------------------------------------
# Sandworms (added after check(), see the module docstring)
# ---------------------------------------------------------------------------
def seed_sandworms(sc: Scenario) -> int:
    """Place the map's worms on open sand, owned by the neutral Fremen house."""
    if not INCLUDE_SANDWORMS:
        return 0
    placed = 0
    for (x, y) in getattr(sc, "worm_sites", ()):
        try:
            wx, wy = sc.free_tile(x, y, radius=12, terrain=SANDY)
        except ValueError:
            continue
        sc.unit("Fremen", "Sandworm", wx, wy, "Area Guard")
        placed += 1
    return placed


# ---------------------------------------------------------------------------
# Extra assertions this map family needs on top of gen_city_scenarios.check()
# ---------------------------------------------------------------------------
def driveable_regions(sc: Scenario) -> Dict[Tuple[int, int], int]:
    """Label every driveable tile with its connected-component id."""
    label: Dict[Tuple[int, int], int] = {}
    nid = 0
    for sy in range(sc.size_y):
        for sx in range(sc.size_x):
            if (sx, sy) in label or sc.canvas.get(sx, sy) not in PASSABLE:
                continue
            nid += 1
            q = deque([(sx, sy)])
            label[(sx, sy)] = nid
            while q:
                x, y = q.popleft()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    t = (x + dx, y + dy)
                    if t in label or not sc.canvas.inside(*t):
                        continue
                    if sc.canvas.get(*t) not in PASSABLE:
                        continue
                    label[t] = nid
                    q.append(t)
    return label


def check_habbanya(sc: Scenario, routes: Sequence[Tuple[int, int]],
                   worms_expected: int) -> Dict[str, object]:
    """Geography and balance assertions specific to these two missions."""
    label = driveable_regions(sc)

    # 1. Every waypoint the mission needs (both starts, every expansion shelf,
    #    both crescent basins, the outer spice sea) is on one driveable body of
    #    ground, so an MCV can actually be driven there.
    ids = set()
    for (x, y) in routes:
        assert (x, y) in label, f"{sc.filename}: waypoint ({x},{y}) is impassable"
        ids.add(label[(x, y)])
    assert len(ids) == 1, \
        f"{sc.filename}: waypoints fall into {len(ids)} separate regions"
    main = ids.pop()

    # 2. Every starting unit stands on that same body of ground, and no unit is
    #    boxed into a pocket of its own.
    for u in sc.units:
        if u.name == "Sandworm":
            continue
        assert label.get((u.x, u.y)) == main, \
            f"{sc.filename}: {u.owner} {u.name} at ({u.x},{u.y}) is cut off"

    # 3. Worms live on open sand, never on a road/structure tile.
    worms = [u for u in sc.units if u.name == "Sandworm"]
    assert len(worms) == worms_expected, \
        f"{sc.filename}: {len(worms)} worms placed, expected {worms_expected}"
    for u in worms:
        assert u.owner == "Fremen", f"{sc.filename}: worm owned by {u.owner}"
        assert sc.canvas.get(u.x, u.y) in SANDY, \
            f"{sc.filename}: worm at ({u.x},{u.y}) is not on sand"
    if worms:
        assert not any(h == "Fremen" for h, _ in sc.houses), \
            f"{sc.filename}: Fremen must stay an undeclared neutral house"

    # 4. The player must not out-produce the enemies put together: this is a
    #    mission, not a handicap match.
    def own(house: str, pred) -> int:
        return sum(1 for s in sc.structures if s.owner == house and pred(s))

    prod = ("Const Yard", "Heavy Factory", "Light Factory", "Hightech Factory",
            "Refinery", "Starport", "Repair Yard", "Barracks", "WOR")
    mine = own("Atreides", lambda s: s.name in prod)
    theirs = sum(own(h, lambda s: s.name in prod)
                 for h, _ in sc.houses if h != "Atreides")
    assert mine * 2 <= theirs * 3, \
        f"{sc.filename}: Atreides production {mine} vs {theirs} combined"
    my_army = sum(1 for u in sc.units
                  if u.owner == "Atreides" and u.name not in ("Harvester", "MCV"))
    foe_army = sum(1 for u in sc.units
                   if u.owner not in ("Atreides", "Fremen")
                   and u.name not in ("Harvester", "MCV"))
    assert my_army <= foe_army, \
        f"{sc.filename}: Atreides army {my_army} vs {foe_army} combined"

    # 5. Rock must not be paved over: leave the player room to zone.
    rock = sum(row.count(ROCK) for row in sc.canvas.rows())
    used = len(sc._blocked) + len(sc._roadset)
    assert used * 100 < rock * 55, \
        f"{sc.filename}: {used} of {rock} rock tiles already built on"

    # 6. Objectives: conquest only.
    assert int(sc.basic["WinFlags"]) == 3 and int(sc.basic["LoseFlags"]) == 1, \
        f"{sc.filename}: these missions are plain 3/1 conquest"

    spice_tiles = sum(row.count(SPICE) + row.count(THICK)
                      for row in sc.canvas.rows())
    return {
        "rock": rock, "built": used, "spice": spice_tiles,
        "worms": len(worms), "regions": len(set(label.values())),
        "army": f"{my_army} vs {foe_army}", "prod": f"{mine} vs {theirs}",
    }


# ===========================================================================
# 1 -- Habbanya Crescent: opposed poles, twin crescents between them
# ===========================================================================
def scenario_habbanya_crescent() -> Scenario:
    W = H = 128
    rng = Rng(0x4ABBA1)
    c = Canvas(W, H)

    habbanya_shell(
        c, rng,
        rim=(64, 64, 57, 59, 7),
        passes=[[(32, 30), (14, 12)], [(96, 30), (114, 12)],
                [(32, 98), (14, 116)], [(96, 98), (114, 116)]],
        crescents=[(44, 50, 20, 17, 5, 64, 64),
                   (86, 80, 20, 17, 5, 64, 64)],
    )

    # Battle lanes: two long flank runs plus the diagonal that threads the two
    # crescent mouths. Everything that matters happens on these.
    lanes(c, rng, [
        ([(30, 14), (22, 40), (26, 68), (34, 96), (46, 114)], 9),
        ([(98, 14), (106, 40), (102, 68), (94, 96), (82, 114)], 9),
        ([(86, 26), (72, 46), (64, 64), (56, 82), (42, 102)], 8),
        ([(28, 64), (48, 58), (64, 64), (80, 70), (100, 62)], 7),
    ])

    # Opposed poles. The Atreides shelf is the smaller one and is boxed in by
    # the rim; the Harkonnen shelf at the south pole is half again as large.
    shelves(c, rng, [
        (60, 24, 11, 6),     # Atreides township -- small, and that is the plot
        (66, 102, 16, 9),    # Harkonnen city
        (26, 34, 7, 5),      # north-west second shelf
        (98, 26, 7, 5),      # north-east second shelf
        (28, 96, 7, 5),      # south-west second shelf
        (102, 92, 7, 5),     # south-east second shelf
        (64, 64, 6, 5),      # the contested plate between the crescent mouths
    ])
    aprons(c, rng, [
        ([(48, 30), (56, 34), (64, 36)], 5),
        ([(58, 90), (66, 88), (76, 92)], 5),
    ])

    spice(c, rng, [
        (24, 22, 8, False), (104, 24, 8, True), (18, 74, 9, True),
        (110, 72, 8, False), (52, 118, 9, True), (78, 12, 7, False),
        (64, 44, 7, False), (64, 84, 7, True), (36, 66, 6, False),
        (98, 58, 6, False), (12, 50, 8, True), (116, 100, 8, True),
    ])
    for (bx, by) in [(26, 60), (100, 46), (64, 48), (64, 80), (40, 110),
                     (92, 112), (16, 20), (112, 26)]:
        bloom(c, bx, by)

    sc = Scenario(
        filename="2P - 128x128 - Habbanya Crescent.ini",
        size_x=W, size_y=H,
        header=[
            "; Habbanya Crescent -- 128x128, 2 houses, DuneCity city-sim scenario.",
            ";",
            "; Terrain after Rippsblack's multiplayer map 'Habbanya-Penny'",
            "; (data/maps/multiplayer, CC-BY-SA): one oval mountain rim around",
            "; the whole bowl, an outer sea of thick spice past it, and two",
            "; C-shaped ranges inside cupping their own spice basins. The",
            "; shapes and seeds here are new; the geography is the homage.",
            ";",
            "; Habbanya Crescent township holds the north pole. It is a small",
            "; town on a small shelf -- one yard, one line of factories, and a",
            "; spare MCV CHOAM would only release against a colony charter. The",
            "; Harkonnen city on the south pole is bigger, richer and already",
            "; has both crescent basins harvested. Take a second shelf before",
            "; you take the south pole; the lanes between the crescents are the",
            "; only ground you can fight on, and the open sand carries worms.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Opponent: Harkonnen -- larger city, larger army, better spice.",
            "; Win     : destroy the Harkonnen. Lose: your buildings gone.",
            "; Generated by scripts/gen_habbanya_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="ATTACK.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 7500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 12000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- Habbanya Crescent township (north pole) --------------------------
    street_path(sc, "Atreides", [(50, 20), (58, 22), (68, 25)], rng, 24)
    street_path(sc, "Atreides", [(52, 28), (60, 29), (69, 30)], rng, 24)
    street_path(sc, "Atreides", [(58, 16), (59, 33)], rng, 12)
    street_path(sc, "Atreides", [(66, 18), (65, 32)], rng, 14)
    spurs(sc, "Atreides", [(50, 20), (68, 25), (69, 30), (52, 28)], rng, 5, (3, 6))

    anchors(sc, "Atreides", [
        ("Const Yard", 59, 24), ("Refinery", 63, 21), ("Windtrap", 55, 22),
        ("Windtrap", 56, 30), ("Light Factory", 67, 27), ("Spice Silo", 52, 25),
        ("Outpost", 62, 28), ("Barracks", 54, 33), ("Repair Yard", 68, 22),
        ("Police Station", 61, 32),
    ])
    zone_districts(sc, "Atreides", [
        District(48, 16, 24, 9, "RRRCCRI", fill=76, limit=18),
        District(48, 25, 24, 10, "RRCCRI", fill=72, limit=14),
    ], rng)
    turret_ring(sc, "Atreides", [(48, 18), (70, 18), (72, 30), (50, 34)], rng)

    force(sc, "Atreides", 60, 38, [
        ("MCV", "Area Guard"),
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Quad", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Launcher", "Area Guard"),
    ])

    # -- Harkonnen south pole --------------------------------------------
    street_path(sc, "Harkonnen", [(54, 96), (66, 97), (80, 99)], rng, 20)
    street_path(sc, "Harkonnen", [(54, 104), (68, 105), (80, 107)], rng, 20)
    street_path(sc, "Harkonnen", [(58, 111), (72, 112)], rng, 18)
    street_path(sc, "Harkonnen", [(62, 92), (63, 113)], rng, 12)
    street_path(sc, "Harkonnen", [(74, 94), (75, 112)], rng, 12)
    spurs(sc, "Harkonnen", [(54, 96), (80, 99), (54, 104), (80, 107)], rng, 7)

    anchors(sc, "Harkonnen", [
        ("Const Yard", 66, 100), ("Refinery", 60, 98), ("Refinery", 76, 104),
        ("Heavy Factory", 70, 103), ("Light Factory", 57, 106),
        ("Hightech Factory", 72, 97), ("Repair Yard", 64, 108),
        ("Windtrap", 56, 100), ("Windtrap", 78, 98), ("Windtrap", 60, 112),
        ("Spice Silo", 52, 102), ("Spice Silo", 80, 110),
        ("Outpost", 68, 94), ("House IX", 74, 108), ("Barracks", 56, 94),
        ("WOR", 78, 112), ("Starport", 68, 110), ("Police Station", 70, 106),
    ])
    zone_districts(sc, "Harkonnen", [
        District(50, 90, 34, 10, "IIRC", fill=80, limit=22),
        District(50, 100, 34, 9, "RRCCRI", fill=82, limit=26),
        District(50, 109, 34, 10, "RRRCI", fill=78, limit=24),
    ], rng)
    turret_ring(sc, "Harkonnen", [(50, 94), (66, 90), (84, 96), (86, 108),
                                  (62, 116), (48, 108)], rng)
    force(sc, "Harkonnen", 66, 86, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Devastator", "Area Guard"), ("Troopers", "Area Guard"),
    ])
    # A standing picket in each crescent mouth: the Harkonnen already own the
    # basins, and taking one back is the mission's first real fight.
    force(sc, "Harkonnen", 50, 54, [
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Tank", "Area Guard"),
    ])
    force(sc, "Harkonnen", 82, 76, [
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Tank", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,5",
        "Harkonnen,Staging,Wheeled,3,7",
        "Harkonnen,Normal,Tracked,4,9",
        "Harkonnen,Kamikaze,Tracked,2,5",
    ]
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,7",
        "Harkonnen,Quad,Enemybase,7",
        "Harkonnen,Tank,Enemybase,16",
        "Harkonnen,Launcher,Enemybase,16",
        "Harkonnen,Siege Tank,Enemybase,26",
        "Harkonnen,Tank,Enemybase,26",
        "Harkonnen,Tank,Enemybase,36,+",
        "Harkonnen,Launcher,Enemybase,36,+",
        # The colony charter: a second hull, then a harvester to work whichever
        # basin the player has managed to hold.
        "Atreides,Harvester,Homebase,9",
        "Atreides,MCV,Homebase,18",
        "Atreides,Harvester,Homebase,25",
        "Atreides,Quad,Homebase,33,+",
    ]
    power_finish(sc)

    # Worm country: both crescent basins, the flank lanes and the outer sea.
    sc.worm_sites = [(44, 50), (86, 80), (22, 44), (24, 72), (106, 44),
                     (102, 74), (64, 60), (64, 70), (8, 64), (120, 64),
                     (64, 6), (64, 122)]
    sc.route_waypoints = [(60, 38), (66, 88), (26, 34), (98, 26), (28, 96),
                          (102, 92), (64, 64), (46, 52), (88, 82),
                          (6, 64), (122, 64)]
    return sc


# ===========================================================================
# 2 -- Habbanya Narrows: mobile colony, two outposts, one waist
# ===========================================================================
def scenario_habbanya_narrows() -> Scenario:
    W = H = 128
    rng = Rng(0x4ABBA2)
    c = Canvas(W, H)

    # The crescents here sit back-to-back across the middle, so the bowl is cut
    # into a north-east approach and a south-west approach joined by one waist
    # at the centre. That waist is the Narrows.
    habbanya_shell(
        c, rng,
        rim=(64, 62, 56, 55, 6),
        passes=[[(30, 24), (10, 6)], [(100, 96), (118, 116)],
                [(64, 106), (64, 126)]],
        crescents=[(48, 44, 19, 16, 4, 26, 20),
                   (80, 82, 19, 16, 4, 102, 106)],
    )

    lanes(c, rng, [
        ([(96, 16), (104, 40), (92, 56), (76, 66)], 10),   # north-east approach
        ([(32, 108), (24, 84), (36, 70), (52, 60)], 10),   # south-west approach
        ([(74, 52), (64, 62), (54, 74)], 7),               # the Narrows itself
        ([(20, 30), (16, 56), (22, 82)], 8),               # west outer road
        ([(108, 44), (112, 70), (104, 96)], 8),            # east outer road
    ])

    shelves(c, rng, [
        (22, 62, 7, 5),      # the Atreides landing shelf, small on purpose
        (88, 34, 11, 7),     # Harkonnen outpost city, north-east pass
        (40, 92, 11, 7),     # Ordos outpost city, south-west pass
        (64, 62, 6, 5),      # the Narrows plate: the contested prize
        (36, 30, 6, 4),      # north-west second shelf, in the west mouth
        (24, 78, 6, 4),      # west second shelf
        (104, 70, 6, 4),     # east second shelf
        (62, 104, 6, 4),     # south second shelf
    ])
    aprons(c, rng, [
        ([(30, 60), (38, 58), (44, 62)], 4),
        ([(82, 46), (86, 50)], 4),
        ([(46, 80), (42, 76)], 4),
    ])

    spice(c, rng, [
        (18, 18, 8, True), (110, 108, 8, True), (68, 22, 9, True),
        (58, 102, 9, True), (48, 44, 8, False), (80, 82, 8, False),
        (24, 46, 7, False), (104, 56, 7, False), (14, 94, 8, True),
        (118, 22, 8, True), (76, 60, 6, False), (52, 68, 6, False),
    ])
    for (bx, by) in [(48, 44), (80, 82), (64, 38), (64, 88), (20, 40),
                     (110, 88), (64, 116), (64, 6)]:
        bloom(c, bx, by)

    sc = Scenario(
        filename="3P - 128x128 - Habbanya Narrows.ini",
        size_x=W, size_y=H,
        header=[
            "; Habbanya Narrows -- 128x128, 3 houses, DuneCity city-sim scenario.",
            ";",
            "; Terrain after Rippsblack's multiplayer map 'Habbanya-Penny'",
            "; (data/maps/multiplayer, CC-BY-SA): the oval mountain rim, the",
            "; outer thick-spice sea and the two C-shaped ranges around their",
            "; own basins. Here the crescents are turned back to back, so the",
            "; bowl is two approaches joined by a single waist -- the Narrows.",
            ";",
            "; You arrive with a colony convoy and a depot, no construction",
            "; yard: two MCVs, a repair yard, a silo and a windtrap on a shelf",
            "; barely big enough to unpack on. Both passes are already held --",
            "; Harkonnen on the north-east, Ordos on the south-west -- and each",
            "; of them runs a working town, not a gang camp. Plant your own",
            "; city first; the Narrows plate in the middle is the only ground",
            "; that reaches both of them.",
            ";",
            "; Player  : Atreides (Brain=Human binds the lobby slot).",
            "; Hostiles: Harkonnen (north-east pass), Ordos (south-west pass),",
            ";           fighting the player and each other.",
            "; Win     : clear both houses. Lose: your buildings gone.",
            "; Generated by scripts/gen_habbanya_scenarios.py -- do not hand-edit.",
        ],
        basic=basic(3, 1, brief="MACHINE.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9500, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 11000, "Quota": 0}),
            ("Ordos", {"Brain": "Team3", "Credits": 10000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(DEFAULT_CHOAM),
    )

    # -- the landing depot: enough to survive, not enough to build ---------
    street_path(sc, "Atreides", [(16, 60), (24, 61), (30, 63)], rng, 20)
    street_path(sc, "Atreides", [(22, 56), (23, 68)], rng, 14)
    anchors(sc, "Atreides", [
        ("Windtrap", 20, 60), ("Spice Silo", 25, 58), ("Repair Yard", 24, 64),
        ("Outpost", 19, 64), ("Barracks", 27, 62),
    ])
    zone_districts(sc, "Atreides", [
        District(14, 54, 20, 16, "RRCI", fill=55, limit=8),
    ], rng)
    turret_ring(sc, "Atreides", [(17, 57), (28, 57), (28, 67)], rng, rocket_every=2)

    force(sc, "Atreides", 26, 70, [
        ("MCV", "Area Guard"), ("MCV", "Area Guard"),
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    # -- Harkonnen, holding the north-east pass ---------------------------
    street_path(sc, "Harkonnen", [(80, 28), (88, 30), (97, 32)], rng, 20)
    street_path(sc, "Harkonnen", [(79, 38), (88, 39), (96, 40)], rng, 20)
    street_path(sc, "Harkonnen", [(84, 24), (85, 43)], rng, 12)
    street_path(sc, "Harkonnen", [(93, 26), (94, 42)], rng, 14)
    street_path(sc, "Harkonnen", [(82, 34), (96, 35)], rng, 16)
    spurs(sc, "Harkonnen", [(80, 28), (97, 32), (79, 38), (96, 40)], rng, 8)

    anchors(sc, "Harkonnen", [
        ("Const Yard", 87, 33), ("Refinery", 83, 30), ("Heavy Factory", 91, 36),
        ("Light Factory", 81, 36), ("Repair Yard", 93, 29),
        ("Windtrap", 85, 27), ("Windtrap", 95, 38), ("Spice Silo", 80, 32),
        ("Outpost", 88, 42), ("House IX", 95, 33), ("Barracks", 82, 42),
        ("WOR", 96, 41), ("Police Station", 90, 27),
    ])
    zone_districts(sc, "Harkonnen", [
        District(76, 22, 26, 10, "IIRC", fill=80, limit=16),
        District(76, 32, 26, 9, "RRCCI", fill=82, limit=20),
        District(76, 41, 26, 8, "RRRCI", fill=78, limit=16),
    ], rng)
    turret_ring(sc, "Harkonnen", [(78, 26), (98, 28), (98, 42), (80, 44)], rng)
    force(sc, "Harkonnen", 86, 52, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])

    # -- Ordos, holding the south-west pass -------------------------------
    street_path(sc, "Ordos", [(31, 86), (40, 88), (49, 90)], rng, 20)
    street_path(sc, "Ordos", [(31, 96), (41, 97), (49, 98)], rng, 20)
    street_path(sc, "Ordos", [(36, 83), (37, 101)], rng, 12)
    street_path(sc, "Ordos", [(45, 85), (46, 99)], rng, 14)
    street_path(sc, "Ordos", [(33, 92), (47, 93)], rng, 16)
    spurs(sc, "Ordos", [(31, 86), (49, 90), (31, 96), (49, 98)], rng, 8)

    anchors(sc, "Ordos", [
        ("Const Yard", 39, 90), ("Refinery", 34, 88), ("Heavy Factory", 43, 94),
        ("Light Factory", 32, 93), ("Repair Yard", 45, 88),
        ("Windtrap", 37, 85), ("Windtrap", 47, 96), ("Spice Silo", 31, 90),
        ("Outpost", 40, 100), ("House IX", 47, 91), ("Barracks", 33, 99),
        ("Police Station", 42, 85),
    ])
    zone_districts(sc, "Ordos", [
        District(28, 80, 26, 10, "IICR", fill=80, limit=14),
        District(28, 90, 26, 9, "RRCCI", fill=82, limit=20),
        District(28, 99, 26, 8, "RRRCI", fill=78, limit=16),
    ], rng)
    turret_ring(sc, "Ordos", [(30, 83), (51, 86), (51, 100), (32, 102)], rng)
    force(sc, "Ordos", 48, 76, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"),
        ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
        ("Launcher", "Area Guard"), ("Deviator", "Area Guard"),
        ("Quad", "Area Guard"), ("Raider Trike", "Area Guard"),
        ("Troopers", "Area Guard"),
    ])

    sc.teams = [
        "Harkonnen,Guard,Foot,2,5",
        "Harkonnen,Normal,Tracked,3,8",
        "Harkonnen,Staging,Wheeled,3,6",
        "Ordos,Guard,Foot,2,5",
        "Ordos,Normal,Tracked,3,8",
        "Ordos,Staging,Wheeled,3,6",
    ]
    sc.reinforcements = [
        "Harkonnen,Trike,Enemybase,10",
        "Harkonnen,Tank,Enemybase,20",
        "Harkonnen,Launcher,Enemybase,30",
        "Harkonnen,Tank,Enemybase,42,+",
        "Ordos,Raider Trike,Enemybase,12",
        "Ordos,Tank,Enemybase,22",
        "Ordos,Deviator,Enemybase,32",
        "Ordos,Launcher,Enemybase,44,+",
        # The colony's own lift: the convoy that was too slow to land with you.
        "Atreides,Harvester,Homebase,8",
        "Atreides,Quad,Homebase,14",
        "Atreides,MCV,Homebase,23",
        "Atreides,Harvester,Homebase,30",
        "Atreides,Tank,Homebase,40,+",
    ]
    power_finish(sc)

    sc.worm_sites = [(48, 44), (80, 82), (64, 62), (68, 22), (58, 102),
                     (18, 20), (110, 106), (104, 56), (24, 46), (6, 62),
                     (122, 62), (64, 120)]
    sc.route_waypoints = [(26, 70), (86, 52), (48, 76), (64, 62), (36, 30),
                          (24, 78), (104, 70), (62, 104), (48, 44), (80, 82),
                          (6, 62), (122, 62)]
    return sc


FACTORIES: List[Callable[[], Scenario]] = [
    scenario_habbanya_crescent,
    scenario_habbanya_narrows,
]


def dump(sc: Scenario) -> None:
    """Terrain proof for review: one character per two tiles."""
    rows = sc.canvas.rows()
    print(f"--- {sc.filename}")
    for y in range(0, sc.size_y, 2):
        print(f"{y:3d} " + "".join(rows[y][x] for x in range(0, sc.size_x, 1)))


def main(argv: Sequence[str]) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for factory in FACTORIES:
        sc = factory()
        stats = check(sc)
        worms = seed_sandworms(sc)
        geo = check_habbanya(sc, sc.route_waypoints, worms)
        text = sc.render()
        check_text(sc, text)
        path = OUT_DIR / sc.filename
        path.write_text(text, encoding="utf-8")
        if "--dump" in argv:
            dump(sc)
        print(f"{sc.filename}: {stats['structures']} structures "
              f"({stats['zones']} zones), {stats['roads']} roads, "
              f"{stats['units']} units + {geo['worms']} worms, "
              f"{len(sc.reinforcements)} reinforcements, "
              f"{path.stat().st_size} bytes")
        print(f"    power {stats['power']} | rock {geo['built']}/{geo['rock']} "
              f"built | spice {geo['spice']} tiles | "
              f"army {geo['army']} | production {geo['prod']}")


if __name__ == "__main__":
    main(sys.argv[1:])
