#!/usr/bin/env python3
"""Deterministic generator for the ten-mission DuneCity campaign (docs/city-campaign.md).

Writes data/maps/singleplayer/DCITY_M01.INI .. DCITY_M10.INI. Each mission starts from a city
layout built with the scripts/gen_city_scenarios.py toolkit (terrain, road-first towns, zoned
districts, power) and adds what a campaign mission needs on top:

  * [OBJECTIVES]  population / treasury / crime / land value goals (MissionObjectives.h)
  * [EVENTS]      timed shield wall breaches (BreachTrigger.h)
  * [BRIEFING]    the Mentat's briefing, win and lose text
  * Brain=TeamN   rivalries the campaign keeps (INIMapLoader, DuneCity campaign only)

The player is always Atreides with a QuantBot helper; the enemies are QuantBots one tier lower
(tests/ai/scenario-matrix.py --categories citycampaign). Missions get harder in order.

Run:  python3 scripts/gen_city_campaign.py
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_city_scenarios as g  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
OUT_DIR = REPO / "data" / "maps" / "singleplayer"


@dataclass
class Mission:
    number: int
    title: str
    build: Callable[[], g.Scenario]
    briefing: str
    win: str
    lose: str
    objectives: Dict[str, int] = field(default_factory=dict)
    events: List[Tuple[str, str]] = field(default_factory=list)
    win_flags: int = 3
    lose_flags: int = 1
    timeout: int = 0
    tech: int = 8
    credits: Dict[str, int] = field(default_factory=dict)
    brains: Dict[str, str] = field(default_factory=dict)
    adjust: Optional[Callable[[g.Scenario], None]] = None
    # Balance levers, measured with tests/ai/scenario-matrix.py --categories citycampaign.
    first_attack: int = 0                 # [EVENTS] FirstEnemyAttack: minutes before enemy waves
    zone_cap: Dict[str, int] = field(default_factory=dict)    # keep at most N R/C/I lots per house
    army_keep: Dict[str, int] = field(default_factory=dict)   # keep at most N starting combat units
    drop_houses: Sequence[str] = ()       # houses removed from the layout entirely
    # Enemy strength: every enemy house's starting credits x strength and the grace period / strength.
    # Calibrated against both gradients (docs/city-campaign.md): one tier up the helper must win,
    # at its own tier it must lose at least 60% of games.
    strength: float = 1.0
    # [DIFFICULTY]: the enemy strength on top of that for the enemy AI's difficulty (Easy ... Brutal),
    # applied by the game when the mission starts (same credits x / grace / rule).
    difficulty: Dict[str, float] = field(default_factory=dict)
    # Low spice (Stefan, 2026-11-19: "these campaign maps ... should have low spice. it is supposed to
    # be about building a city"): thick spice and blooms go, and every field is eroded from its edge
    # until at most this share of the layout's spice tiles is left.
    spice_keep: float = 0.25
    # Cities need rock (Stefan, 2026-11-19: "remember we need rock for building cities"): the rock
    # shelves grow ring by ring into the sand around them until at least this share of the map is
    # rock (0 = the layout as drawn).
    rock_floor: float = 0.30
    # Grow rock only inside this disc (x, y, radius); the floor is then that disc's rock share.
    rock_area: Optional[Tuple[int, int, int]] = None
    # A compact layout (at most 64x64 = 4,096 tiles) for this mission, where every difficulty's
    # map-size army and city limits bind inside the first 15 minutes (docs/city-campaign.md).
    compact: Optional[Callable[[], g.Scenario]] = None


def set_house(sc: g.Scenario, name: str, **values) -> None:
    for i, (house, body) in enumerate(sc.houses):
        if house == name:
            body = dict(body)
            body.update(values)
            sc.houses[i] = (house, body)
            return
    raise KeyError(f"{sc.filename}: no house {name}")


def drop_reinforcements(sc: g.Scenario, owner: str) -> None:
    sc.reinforcements = [r for r in sc.reinforcements if not r.startswith(owner + ",")]


ZONES = ("Residential Zone", "Commercial Zone", "Industrial Zone")
NON_COMBAT = ("Harvester", "Carryall", "MCV", "Sandworm")


def cap_zones(sc: g.Scenario, owner: str, keep: int) -> None:
    """Keep the first `keep` R/C/I lots of a house (generation order: its core districts first)."""
    kept = 0
    out = []
    for s in sc.structures:
        if s.owner == owner and s.name in ZONES:
            kept += 1
            if kept > keep:
                continue
        out.append(s)
    sc.structures = out


def cap_army(sc: g.Scenario, owner: str, keep: int) -> None:
    """Keep at most `keep` starting combat units of a house; workers and transports stay."""
    kept = 0
    out = []
    for u in sc.units:
        if u.owner == owner and u.name not in NON_COMBAT:
            kept += 1
            if kept > keep:
                continue
        out.append(u)
    sc.units = out


def drop_house(sc: g.Scenario, owner: str) -> None:
    sc.structures = [s for s in sc.structures if s.owner != owner]
    sc.units = [u for u in sc.units if u.owner != owner]
    sc.roads = [r for r in sc.roads if r[0] != owner]
    sc.houses = [h for h in sc.houses if h[0] != owner]
    sc.reinforcements = [r for r in sc.reinforcements if not r.startswith(owner + ",")]
    sc.teams = [t for t in sc.teams if not t.startswith(owner + ",")]


def thin_spice(sc: g.Scenario, keep: float) -> Tuple[int, int]:
    """Low-spice campaign maps: thick spice becomes ordinary spice, blooms become sand, and each
    field loses its outer ring of tiles, pass after pass, while more than `keep` of the original
    spice tiles remain. Field positions stay; small fields vanish, large ones keep their cores.
    Returns (spice tiles before, after)."""
    c = sc.canvas
    for y in range(c.h):
        for x in range(c.w):
            if c.g[y][x] == g.THICK:
                c.g[y][x] = g.SPICE
            elif c.g[y][x] == g.BLOOM:
                c.g[y][x] = g.SAND
    tiles = {(x, y) for y in range(c.h) for x in range(c.w) if c.g[y][x] == g.SPICE}
    before = len(tiles)
    target = int(before * keep)
    while len(tiles) > target:
        edge = sorted(t for t in tiles
                      if any((t[0] + dx, t[1] + dy) not in tiles for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))))
        if not edge:
            break
        # The last pass stops at the target: drop edge tiles evenly across the map, not row by row.
        excess = len(tiles) - target
        drop = edge if len(edge) <= excess else edge[::max(1, len(edge) // excess)][:excess]
        for (x, y) in drop:
            tiles.discard((x, y))
            c.g[y][x] = g.SAND
    return before, len(tiles)


def grow_rock(sc: g.Scenario, floor: float,
              area: Optional[Tuple[int, int, int]] = None) -> Tuple[int, int]:
    """Room to build: every rock shelf grows one ring at a time into the open sand and dunes beside
    it (never onto spice or mountains), until `floor` of the map (or of `area`, a disc x, y,
    radius) is rock. Towns, roads and structures already stand on rock and stay where they are.
    Returns (rock tiles before, after) within the map or area."""
    c = sc.canvas
    def inside(x: int, y: int) -> bool:
        return c.inside(x, y) and (area is None or (x - area[0]) ** 2 + (y - area[1]) ** 2 <= area[2] ** 2)
    cells = [(x, y) for y in range(c.h) for x in range(c.w) if inside(x, y)]
    rock = {t for t in cells if c.g[t[1]][t[0]] == g.ROCK}
    before = len(rock)
    target = int(len(cells) * floor)
    while len(rock) < target:
        ring = sorted({(x + dx, y + dy) for (x, y) in rock for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                       if inside(x + dx, y + dy) and c.g[y + dy][x + dx] in (g.SAND, g.DUNES)})
        if not ring:
            break
        need = target - len(rock)
        add = ring if len(ring) <= need else ring[::max(1, len(ring) // need)][:need]
        for (x, y) in add:
            c.g[y][x] = g.ROCK
            rock.add((x, y))
    return before, len(rock)


def render(m: Mission, sc: g.Scenario) -> str:
    thin_spice(sc, m.spice_keep)
    if m.rock_floor:
        grow_rock(sc, m.rock_floor, m.rock_area)
    sc.filename = f"DCITY_M{m.number:02d}.INI"
    sc.basic = g.basic(m.win_flags, m.lose_flags, m.timeout, m.tech,
                       brief=sc.basic.get("BriefPicture", "ATTACK.WSA"),
                       winpic=sc.basic.get("WinPicture", "WIN1.WSA"))
    if not m.win_flags & 4:
        # Campaign treasury goals replace the old spice quota.
        for house, _ in sc.houses:
            set_house(sc, house, Quota=0)
    for house, amount in m.credits.items():
        set_house(sc, house, Credits=amount)
    for house, brain in m.brains.items():
        set_house(sc, house, Brain=brain)
    if m.strength != 1.0:
        for house, body in list(sc.houses):
            if house != "Atreides":
                set_house(sc, house, Credits=int(round(int(body["Credits"]) * m.strength / 100.0)) * 100)
    for house in m.drop_houses:
        drop_house(sc, house)
    for house, keep in m.zone_cap.items():
        cap_zones(sc, house, keep)
    for house, keep in m.army_keep.items():
        cap_army(sc, house, keep)
    if m.adjust:
        m.adjust(sc)
    sc.header = [
        f"; DuneCity campaign, mission {m.number}: {m.title}.",
        "; Played from the campaign menu (Maps: DuneCity) as Atreides; docs/city-campaign.md.",
        "; Generated by scripts/gen_city_campaign.py -- do not hand-edit.",
    ]
    text = sc.render()
    extra: List[str] = []
    events = list(m.events)
    if m.first_attack:
        grace = max(1, int(round(m.first_attack / m.strength)))
        events.insert(0, ("FirstEnemyAttack", str(grace)))
    if m.objectives:
        extra.append("[OBJECTIVES]")
        extra += [f"{k}={v}" for k, v in m.objectives.items()]
        extra.append("")
    if events:
        extra.append("[EVENTS]")
        extra += [f"{k}={v}" for k, v in events]
        extra.append("")
    if m.difficulty:
        extra.append("[DIFFICULTY]")
        extra += [f"{k}={v:g}" for k, v in m.difficulty.items()]
        extra.append("")
    extra.append("[BRIEFING]")
    extra.append(f"Text={m.briefing}")
    extra.append(f"Win={m.win}")
    extra.append(f"Lose={m.lose}")
    extra.append("")
    return text + "\n".join(extra) + "\n"


# ---------------------------------------------------------------------------
# Compact layouts (64x64): the difficulties' map-size limits decide these maps
# ---------------------------------------------------------------------------
def compact_sihaya_basin() -> g.Scenario:
    """Mission 1 on 64x64: a crowded home shelf, three small rock islands to colonise and the
    Harkonnen on the wide eastern shelf with the best spice."""
    W = H = 64
    rng = g.Rng(0x51A64)
    c = g.Canvas(W, H)
    c.organic(13, 15, 9, g.ROCK, rng, lobes=6)                        # Atreides shelf
    c.ribbon([(6, 24), (12, 28), (18, 28)], g.ROCK, 4, rng)          # apron
    c.organic(49, 45, 11, g.ROCK, rng, lobes=8)                       # Harkonnen shelf
    c.ribbon([(36, 34), (38, 46), (36, 58)], g.MOUNTAIN, 3, rng)      # their western ridge
    c.rect(36, 44, 4, 4, g.SAND)                                      # the one gap
    for (x, y, r) in [(40, 12, 6), (14, 48, 6), (28, 34, 5)]:          # colony islands
        c.organic(x, y, r, g.ROCK, rng, lobes=5)
    for (sx, sy, r, rich) in [(26, 10, 4, False), (28, 22, 5, True), (6, 38, 4, False),
                              (24, 50, 5, True), (52, 22, 5, True), (58, 60, 4, False),
                              (44, 30, 3, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    c.set(32, 42, g.BLOOM)
    sc = g.Scenario(
        filename="2P - 64x64 - Sihaya Basin.ini", size_x=W, size_y=H,
        header=["; Sihaya Basin (compact) -- 64x64, 2 houses, DuneCity campaign mission 1."],
        basic=g.basic(3, 1, brief="ATTACK.WSA"),
        houses=[("Atreides", {"Brain": "Human", "Credits": 7000, "Quota": 0}),
                ("Harkonnen", {"Brain": "Team2", "Credits": 9000, "Quota": 0})],
        canvas=c, choam=dict(g.DEFAULT_CHOAM),
    )
    g.street_path(sc, "Atreides", [(6, 12), (13, 13), (20, 15)], rng, 14)
    g.street_path(sc, "Atreides", [(8, 19), (15, 20), (20, 19)], rng, 12)
    g.street_path(sc, "Atreides", [(12, 8), (13, 24)], rng, 8)
    g.anchors(sc, "Atreides", [("Const Yard", 11, 15), ("Refinery", 16, 11), ("Windtrap", 8, 13),
                               ("Windtrap", 9, 19), ("Light Factory", 17, 19), ("Spice Silo", 14, 23)])
    g.zone_districts(sc, "Atreides", [g.District(5, 8, 18, 8, "RRRCI", fill=80, limit=10),
                                      g.District(5, 16, 18, 9, "RRCCI", fill=70, limit=6)], rng)
    g.force(sc, "Atreides", 12, 26, [("MCV", "Area Guard"), ("Harvester", "Harvest"),
                                     ("Harvester", "Harvest"), ("Tank", "Area Guard"),
                                     ("Quad", "Area Guard"), ("Trike", "Area Guard")], spread=2)
    g.street_path(sc, "Harkonnen", [(42, 41), (50, 42), (58, 44)], rng, 14)
    g.street_path(sc, "Harkonnen", [(42, 49), (50, 50), (58, 50)], rng, 14)
    g.street_path(sc, "Harkonnen", [(49, 36), (50, 56)], rng, 10)
    g.anchors(sc, "Harkonnen", [("Const Yard", 47, 45), ("Refinery", 53, 40), ("Heavy Factory", 52, 46),
                                ("Light Factory", 44, 46), ("Windtrap", 45, 40), ("Windtrap", 56, 47),
                                ("Windtrap", 46, 52), ("Spice Silo", 54, 52), ("Barracks", 58, 41)])
    g.zone_districts(sc, "Harkonnen", [g.District(40, 36, 22, 9, "IIRC", fill=78, limit=10),
                                       g.District(40, 45, 22, 12, "RRCCI", fill=78, limit=10)], rng)
    g.turret_ring(sc, "Harkonnen", [(40, 40), (40, 52), (50, 34), (60, 54)], rng)
    g.force(sc, "Harkonnen", 50, 54, [("Harvester", "Harvest"), ("Harvester", "Harvest"),
                                      ("Tank", "Area Guard"), ("Tank", "Area Guard"),
                                      ("Quad", "Area Guard"), ("Trike", "Area Guard"),
                                      ("Launcher", "Area Guard")], spread=2)
    sc.teams = ["Harkonnen,Staging,Wheeled,2,5", "Harkonnen,Normal,Tracked,3,7"]
    sc.reinforcements = ["Atreides,Harvester,Homebase,10", "Atreides,MCV,Homebase,20"]
    g.power_finish(sc)
    return sc


# ---------------------------------------------------------------------------
# Mission 7: the Shield Wall breach
# ---------------------------------------------------------------------------
SHIELD_WALL_BREACH = (66, 54, 13, 20)   # x, y, width, height of the gap the sappers open


def scenario_shield_wall_breach() -> g.Scenario:
    """The Atreides city sits in a basin ringed by the Shield Wall, with its own sand and spice
    and no worms. The only ways out are two narrow western passes, so the Harkonnen and
    Sardaukar on the eastern flats must march the long way round - until the eastern wall is
    breached (SHIELD_WALL_BREACH) and worms and armies come straight through."""
    import math
    W, H = 144, 128
    rng = g.Rng(0x5B1E7)
    c = g.Canvas(W, H)
    cx, cy, radius = 40, 64, 33
    # The basin floor: sand with spice, and a broad rock shelf for the city.
    c.organic(cx - 2, cy, 19, g.ROCK, rng, lobes=9)
    c.spice_field(cx + 14, cy - 18, 7, rng, True)
    c.spice_field(cx + 15, cy + 18, 7, rng, True)
    # The Shield Wall: a thick mountain ring, open only at two narrow western passes.
    pts = []
    for deg in range(0, 361, 6):
        a = math.radians(deg)
        pts.append((deg, int(cx + radius * math.cos(a)), int(cy + radius * math.sin(a))))
    gaps = [(150, 162), (198, 210)]
    run = []
    for deg, x, y in pts:
        if any(lo <= deg <= hi for lo, hi in gaps):
            if len(run) > 1:
                c.ribbon(run, g.MOUNTAIN, 6, rng)
            run = []
        else:
            run.append((x, y))
    if len(run) > 1:
        c.ribbon(run, g.MOUNTAIN, 6, rng)
    # Make sure the eastern wall is solid where the breach will open.
    bx, by, bw, bh = SHIELD_WALL_BREACH
    c.rect(bx + 1, by, bw - 2, bh, g.MOUNTAIN)
    # Outside: the eastern flats, two rival bases, open rock for the west passes' outposts.
    c.organic(118, 34, 16, g.ROCK, rng, lobes=9)    # Harkonnen
    c.organic(120, 98, 15, g.ROCK, rng, lobes=8)    # Sardaukar
    c.organic(8, 20, 7, g.ROCK, rng, lobes=4)
    c.organic(8, 110, 7, g.ROCK, rng, lobes=4)
    for (sx, sy, r, rich) in [(96, 64, 11, True), (92, 14, 8, False), (94, 116, 8, False),
                              (136, 66, 7, True), (14, 64, 6, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(86, 40), (86, 88)]:
        c.organic(dx, dy, 5, g.DUNES, rng, lobes=4, only_if=(g.SAND,))
    c.set(104, 64, g.BLOOM)

    sc = g.Scenario(
        filename="3P - 144x128 - Shield Wall Breach.ini",
        size_x=W, size_y=H, header=[],
        basic=g.basic(3, 1, brief="SARDUKAR.WSA", winpic="WIN2.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 9000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 9000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 7000, "Quota": 0}),
        ],
        canvas=c, choam=dict(g.DEFAULT_CHOAM),
    )
    # -- the Atreides basin city ----------------------------------------------
    ax, ay = cx - 4, cy
    g.street_path(sc, "Atreides", [(ax - 14, ay - 2), (ax, ay), (ax + 14, ay - 2)], rng, 14)
    g.street_path(sc, "Atreides", [(ax, ay - 14), (ax + 1, ay + 14)], rng, 10)
    g.street_path(sc, "Atreides", [(ax - 9, ay - 10), (ax - 8, ay + 10)], rng, 10)
    g.spurs(sc, "Atreides", [(ax - 14, ay - 2), (ax + 14, ay - 2)], rng, 4)
    g.anchors(sc, "Atreides", [
        ("Const Yard", ax, ay - 4), ("Refinery", ax + 6, ay - 8), ("Windtrap", ax - 4, ay - 6),
        ("Windtrap", ax + 4, ay + 4), ("Windtrap", ax - 6, ay + 4), ("Spice Silo", ax + 8, ay + 2),
        ("Light Factory", ax - 10, ay - 4), ("Heavy Factory", ax + 9, ay + 7), ("Barracks", ax - 12, ay + 6),
        ("Outpost", ax + 2, ay + 8), ("Police Station", ax - 2, ay + 10),
    ])
    g.zone_districts(sc, "Atreides", [
        g.District(ax - 16, ay - 14, 32, 12, "RRCIR", fill=72, limit=10),
        g.District(ax - 16, ay + 2, 32, 14, "RRCCI", fill=72, limit=10),
    ], rng)
    g.force(sc, "Atreides", ax + 6, ay + 16, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Carryall", "Area Guard"),
        ("Tank", "Area Guard"), ("Tank", "Area Guard"), ("Launcher", "Area Guard"),
        ("Quad", "Area Guard"), ("Trike", "Area Guard"), ("Troopers", "Area Guard"),
    ])
    # -- the rivals on the eastern flats ----------------------------------------
    for owner, (rx, ry), extra, army in [
        ("Harkonnen", (118, 34), [("Heavy Factory", 124, 30), ("Hightech Factory", 110, 40), ("Repair Yard", 126, 40)],
         [("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
          ("Siege Tank", "Area Guard"), ("Launcher", "Area Guard"), ("Devastator", "Area Guard"), ("Quad", "Area Guard")]),
        ("Sardaukar", (120, 98), [("Heavy Factory", 126, 94), ("WOR", 112, 104)],
         [("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Siege Tank", "Area Guard"),
          ("Launcher", "Area Guard"), ("Troopers", "Area Guard"), ("Troopers", "Area Guard")]),
    ]:
        g.street_path(sc, owner, [(rx - 11, ry), (rx, ry + 1), (rx + 11, ry)], rng, 14)
        g.street_path(sc, owner, [(rx, ry - 10), (rx + 1, ry + 10)], rng, 10)
        g.anchors(sc, owner, [
            ("Const Yard", rx, ry - 3), ("Refinery", rx - 6, ry - 5), ("Windtrap", rx + 4, ry - 6),
            ("Windtrap", rx - 4, ry + 3), ("Windtrap", rx + 6, ry + 4), ("Light Factory", rx - 8, ry + 5),
            ("Barracks", rx + 2, ry + 6), ("Outpost", rx - 2, ry - 8),
        ] + extra)
        g.zone_districts(sc, owner, [g.District(rx - 13, ry - 12, 26, 24, "RRCCI", fill=62, limit=10)], rng)
        g.turret_ring(sc, owner, [(rx - 12, ry - 6), (rx - 12, ry + 6), (rx - 6, ry - 12), (rx - 6, ry + 12)], rng)
        g.force(sc, owner, rx - 14, ry, army)
    sc.reinforcements = [
        "Atreides,Harvester,Homebase,9",
        "Harkonnen,Tank,Enemybase,14",
        "Harkonnen,Siege Tank,Enemybase,16,+",
        "Sardaukar,Troopers,Enemybase,18,+",
        "Atreides,Launcher,Homebase,20",
    ]
    g.power_finish(sc)
    return sc


# ---------------------------------------------------------------------------
# Mission 10: All Against Atreides, rebuilt for city building
# ---------------------------------------------------------------------------
def scenario_all_against_atreides_city() -> g.Scenario:
    """The bundled All against Atreides premise (one Atreides capital against four allied
    houses) on a 160x160 map with room to build a city on both sides: a broad Atreides
    plateau in the north with its own spice, and four rival plateaus with space to grow."""
    W, H = 160, 160
    rng = g.Rng(0xAA7C1)
    c = g.Canvas(W, H)

    # The Atreides capital: a wide northern plateau behind a broken mountain arc.
    c.organic(80, 30, 26, g.ROCK, rng, lobes=11)
    c.ribbon([(36, 8), (52, 4), (80, 3), (108, 4), (124, 8)], g.MOUNTAIN, 5, rng)
    c.ribbon([(44, 58), (60, 64)], g.MOUNTAIN, 5, rng)
    c.ribbon([(100, 64), (116, 58)], g.MOUNTAIN, 5, rng)
    # The four allied houses, each on its own plateau with room to expand.
    rivals = {
        "Fremen": (22, 74), "Sardaukar": (138, 74),
        "Ordos": (34, 132), "Harkonnen": (126, 132),
    }
    for (rx, ry) in rivals.values():
        c.organic(rx, ry, 17, g.ROCK, rng, lobes=9)
    # Contested middle ground: open rock for expansions and a central spice basin.
    c.organic(80, 100, 11, g.ROCK, rng, lobes=6)
    c.ribbon([(60, 118), (80, 128), (100, 118)], g.MOUNTAIN, 6, rng)

    for (sx, sy, r, rich) in [(40, 34, 10, True), (120, 34, 10, True), (80, 70, 11, True),
                              (52, 92, 9, True), (108, 92, 9, True), (80, 148, 10, True),
                              (12, 104, 8, False), (148, 104, 8, False), (20, 40, 7, False),
                              (140, 40, 7, False)]:
        c.spice_field(sx, sy, r, rng, rich)
    for (dx, dy) in [(60, 48), (100, 48), (80, 116)]:
        c.organic(dx, dy, 5, g.DUNES, rng, lobes=4, only_if=(g.SAND,))
    c.set(80, 84, g.BLOOM)
    c.set(30, 100, g.BLOOM)
    c.set(130, 100, g.BLOOM)

    sc = g.Scenario(
        filename="5P - 160x160 - All Against Atreides City.ini",
        size_x=W, size_y=H,
        header=[],
        basic=g.basic(3, 1, brief="SARDUKAR.WSA", winpic="WIN2.WSA"),
        houses=[
            ("Atreides", {"Brain": "Human", "Credits": 30000, "Quota": 0}),
            ("Fremen", {"Brain": "Team2", "Credits": 5000, "Quota": 0}),
            ("Sardaukar", {"Brain": "Team2", "Credits": 5000, "Quota": 0}),
            ("Ordos", {"Brain": "Team2", "Credits": 5000, "Quota": 0}),
            ("Harkonnen", {"Brain": "Team2", "Credits": 5000, "Quota": 0}),
        ],
        canvas=c,
        choam=dict(g.DEFAULT_CHOAM),
    )

    # -- the Atreides capital -------------------------------------------------
    ax, ay = 80, 30
    g.street_path(sc, "Atreides", [(ax - 18, ay), (ax, ay + 2), (ax + 18, ay)], rng, 14)
    g.street_path(sc, "Atreides", [(ax, ay - 12), (ax + 1, ay + 14)], rng, 10)
    g.street_path(sc, "Atreides", [(ax - 10, ay - 8), (ax - 9, ay + 10)], rng, 10)
    g.spurs(sc, "Atreides", [(ax - 18, ay), (ax + 18, ay)], rng, 5)
    g.anchors(sc, "Atreides", [
        ("Const Yard", ax, ay - 4), ("Refinery", ax - 6, ay - 6), ("Windtrap", ax + 4, ay - 6),
        ("Windtrap", ax - 4, ay + 4), ("Windtrap", ax + 6, ay + 4), ("Spice Silo", ax + 8, ay - 4),
        ("Light Factory", ax - 12, ay + 4), ("Barracks", ax + 12, ay + 4), ("Outpost", ax + 2, ay + 6),
    ])
    g.zone_districts(sc, "Atreides", [
        g.District(ax - 20, ay - 12, 40, 10, "RRCIR", fill=70, limit=8),
        g.District(ax - 20, ay + 2, 40, 12, "RRCCI", fill=70, limit=8),
    ], rng)
    g.force(sc, "Atreides", ax, ay + 16, [
        ("Harvester", "Harvest"), ("Harvester", "Harvest"), ("Carryall", "Area Guard"),
        ("Siege Tank", "Area Guard"), ("Siege Tank", "Area Guard"), ("Tank", "Area Guard"),
        ("Tank", "Area Guard"), ("Launcher", "Area Guard"), ("Quad", "Area Guard"),
        ("Trike", "Area Guard"), ("Troopers", "Area Guard"), ("Troopers", "Area Guard"),
    ])

    # -- four allied rivals, each a small working town --------------------------
    special = {"Harkonnen": "Devastator", "Ordos": "Deviator", "Sardaukar": "Siege Tank", "Fremen": "Siege Tank"}
    for owner, (rx, ry) in rivals.items():
        g.street_path(sc, owner, [(rx - 10, ry), (rx, ry + 1), (rx + 10, ry)], rng, 14)
        g.street_path(sc, owner, [(rx, ry - 9), (rx + 1, ry + 9)], rng, 10)
        g.anchors(sc, owner, [
            ("Const Yard", rx, ry - 3), ("Refinery", rx - 5, ry - 5), ("Windtrap", rx + 4, ry - 5),
            ("Windtrap", rx - 4, ry + 3), ("Light Factory", rx + 5, ry + 3), ("Barracks", rx - 8, ry + 6),
            ("Outpost", rx + 2, ry + 5),
        ])
        if owner in ("Harkonnen", "Sardaukar"):
            g.anchors(sc, owner, [("Heavy Factory", rx + 8, ry - 2), ("Windtrap", rx + 9, ry + 6)])
        g.zone_districts(sc, owner, [g.District(rx - 12, ry - 10, 24, 20, "RRCI", fill=60, limit=6)], rng)
        g.turret_ring(sc, owner, [(rx - 9, ry - 8), (rx + 9, ry - 8), (rx + 9, ry + 9), (rx - 9, ry + 9)], rng)
        g.force(sc, owner, rx, ry + 12, [
            ("Harvester", "Harvest"), ("Tank", "Area Guard"), ("Tank", "Area Guard"),
            ("Launcher", "Area Guard"), ("Quad", "Area Guard"), (special[owner], "Area Guard"),
            ("Troopers", "Area Guard"),
        ])
    sc.reinforcements = [
        "Atreides,Harvester,Homebase,8",
        "Atreides,Siege Tank,Homebase,20",
        "Harkonnen,Tank,Homebase,18,+",
        "Ordos,Tank,Homebase,22,+",
        "Sardaukar,Troopers,Homebase,26,+",
        "Fremen,Troopers,Homebase,30,+",
    ]
    g.power_finish(sc)
    return sc


# ---------------------------------------------------------------------------
# The missions, easiest first
# ---------------------------------------------------------------------------

MISSIONS: List[Mission] = [
    Mission(
        1, "Sihaya Basin", g.scenario_sihaya_basin,
        briefing=("The Basin township has outgrown its shelf. CHOAM has released two mothballed MCVs. "
                  "Settle the open rock islands and grow the colony to 20,000 people. "
                  "The Harkonnen on the eastern shelf will raid your harvesters; destroying them also ends the mission."),
        win="Sihaya is a city now. The Landsraad has noticed.",
        lose="The Basin is lost. The Harkonnen own the shelf.",
        objectives={"Population": 20000, "HoldSeconds": 60},
        credits={"Atreides": 12000, "Harkonnen": 5000},
        zone_cap={"Harkonnen": 30}, first_attack=10, compact=compact_sihaya_basin,
        difficulty={"Easy": 6.8, "Medium": 5, "Hard": 3.25, "Brutal": 3.35},
    ),
    Mission(
        2, "Hagal Flats", g.scenario_hagal_flats,
        briefing=("CHOAM wants a strong treasury on Hagal before it will finance the next colony. "
                  "Bank 30,000 credits. Taxes and refined spice both count, but only what your silos can store. "
                  "Two rival houses will raid the spice fields."),
        win="The treasury is full. CHOAM has opened its accounts to House Atreides.",
        lose="Hagal is bankrupt and overrun.",
        objectives={"Credits": 30000, "HoldSeconds": 30},
        credits={"Atreides": 12000, "Ordos": 4000, "Harkonnen": 4000},
        zone_cap={"Ordos": 16, "Harkonnen": 16}, first_attack=9,
        difficulty={"Easy": 4.3, "Medium": 2.95, "Hard": 2.95, "Brutal": 3.25},
    ),
    Mission(
        3, "Ash Quarter", g.scenario_ash_quarter,
        briefing=("The Ash Quarter's tenements have been abandoned to gangs. Restore order: bring crime "
                  "down to Safe and grow the city to 75,000 people. Police stations, fewer polluted blocks "
                  "and higher land value all cut crime. Harkonnen and Mercenaries profit from the chaos."),
        win="The Quarter is safe again. Its people have returned.",
        lose="The gangs and their paymasters have taken the city.",
        objectives={"Population": 75000, "MaxCrime": 63, "HoldSeconds": 60},
        credits={"Atreides": 14000, "Harkonnen": 7000, "Mercenary": 4000},
        # Grace 18 (was 9): with 9 every helper tier was overrun at minutes 10-12, so a Brutal helper
        # won no more often than a Hard one; doubled, Brutal v Hard won 3/4 where Hard v Hard won 0/4.
        army_keep={"Harkonnen": 6}, first_attack=18,
        difficulty={"Easy": 6, "Medium": 3.3, "Hard": 2.85, "Brutal": 2.95},
    ),
    Mission(
        4, "Arrakeen Blackout", g.scenario_arrakeen_blackout,
        briefing=("Arrakeen has lost its power grid and its people are leaving. Restore power, rebuild the "
                  "districts and lift the city to 60,000 people with middle-class land value. "
                  "Harkonnen and Mercenary raiders will strike while the lights are out."),
        win="Arrakeen shines again. The Duke's capital is secure.",
        lose="Arrakeen is dark and its people are gone.",
        objectives={"Population": 60000, "MinLandValue": 90, "HoldSeconds": 60},
        # Grace 45 (was 8): the grace is divided by the enemy's [DIFFICULTY], so 8 minutes at 3.5 sent the
        # first wave at minute 2 and every helper tier was overrun at minutes 10-13 (Brutal v Hard won no
        # more often than Hard v Hard). A Brutal helper reaches the goals at minutes 11-16, a Hard one at
        # 19-24: the grace now ends between the two.
        credits={"Atreides": 15000, "Mercenary": 6000, "Harkonnen": 6000}, first_attack=45,
        difficulty={"Easy": 8.4, "Medium": 3.6, "Hard": 3.2, "Brutal": 3.35},
    ),
    Mission(
        5, "Cielago Watch", g.scenario_cielago_watch,
        briefing=("The Watch holds a relay mesa between Harkonnen and Ordos territory. CHOAM will lift the "
                  "garrison out in 30 minutes. Until then the Watch must hold - or destroy both houses first."),
        win="The Watch held. The garrison is relieved with honour.",
        lose="The Watch has fallen.",
        # Holding out wins at the timer; destroying both houses wins earlier.
        win_flags=11, lose_flags=9, timeout=30,
        credits={"Atreides": 13000, "Harkonnen": 10000, "Ordos": 10000}, first_attack=3,
        difficulty={"Easy": 1.2, "Medium": 1.35, "Hard": 1.55, "Brutal": 1.6},
    ),
    Mission(
        6, "Harg Pass Convoy", g.scenario_harg_pass_convoy,
        briefing=("Five supply towns share one construction yard along the Harg Pass. Protect the useful "
                  "links, grow what you can and destroy the Harkonnen and Mercenary bases that raid them."),
        win="The pass is open. Spice flows to Arrakeen again.",
        lose="The pass is closed and the towns are burning.",
        credits={"Atreides": 10000, "Harkonnen": 14000, "Mercenary": 8000}, first_attack=3,
        difficulty={"Easy": 0.85, "Medium": 0.95, "Hard": 1, "Brutal": 1.1},
    ),
    Mission(
        7, "Shield Wall Breach", scenario_shield_wall_breach,
        briefing=("Your city lies in a basin behind the Shield Wall, safe from worms, reached only through two "
                  "narrow western passes. Harkonnen sappers are mining the eastern wall: at the twelfth minute it "
                  "will break open, and sandworms and the Harkonnen and Sardaukar armies will come straight "
                  "through the breach. Hold the basin and grow the city to 50,000 people - or destroy both houses."),
        objectives={"Population": 50000, "HoldSeconds": 60},
        win="The breach is held and the invaders are destroyed. The basin is safe.",
        lose="The Shield Wall fell, and your city with it.",
        credits={"Atreides": 16000, "Harkonnen": 7000, "Sardaukar": 5000},
        # Grace 40 (was 10, then 30): the attack waves wait for the breach to play out, so the helper's own
        # play decides the mission. The grace is divided by the enemy's [DIFFICULTY]: 30 at 1.4-1.7 sent
        # the first wave at minutes 18-21, before either helper tier had 50,000 people, and Brutal v Hard
        # won no more often than Hard v Hard. A Brutal helper reaches the goal at minutes 22-31, a Hard
        # one at 29-36.
        army_keep={"Harkonnen": 5, "Sardaukar": 4}, first_attack=40,
        rock_floor=0.75, rock_area=(40, 64, 28),   # the basin only: the flats keep the worms' sand
        events=[("Breach1", "12,%d,%d,%d,%d,2" % SHIELD_WALL_BREACH),
                ("Breach1Message", "The Shield Wall has been breached! Sandworms and Harkonnen armies are coming through.")],
        difficulty={"Easy": 1.55, "Medium": 1.3, "Hard": 1.7, "Brutal": 1.65},
    ),
    Mission(
        8, "Tuono Crossing", g.scenario_tuono_crossing,
        briefing=("No construction yard, a supply ledge and a mobile column. Deploy the MCV on open rock, "
                  "found a colony of 12,000 people and bank 30,000 credits before the Harkonnen and Sardaukar "
                  "can stop you - or destroy them both."),
        win="Tuono Crossing is a city. The Emperor's legions withdraw.",
        lose="The expedition has failed.",
        objectives={"Population": 12000, "Credits": 30000, "HoldSeconds": 60},
        credits={"Atreides": 14000, "Harkonnen": 4000, "Sardaukar": 6000}, first_attack=13,
        difficulty={"Easy": 2.15, "Medium": 0.9, "Hard": 0.95, "Brutal": 1.05},
    ),
    Mission(
        9, "Carthag Vise", g.scenario_carthag_vise,
        briefing=("Carthag's valley city faces armies from the north and the south and a Sardaukar battery "
                  "on the ridge. Hold the core, keep the refineries running and destroy all three hostile houses."),
        win="The Vise is broken. Carthag is ours.",
        lose="Carthag has fallen to the Harkonnen.",
        credits={"Atreides": 21000, "Harkonnen": 4000, "Ordos": 4000, "Sardaukar": 1500},
        zone_cap={"Harkonnen": 10, "Ordos": 10}, army_keep={"Harkonnen": 2, "Ordos": 2, "Sardaukar": 2},
        first_attack=10,
        difficulty={"Easy": 2.55, "Medium": 2.3, "Hard": 2.4, "Brutal": 2.5},
    ),
    Mission(
        10, "All Against Atreides", scenario_all_against_atreides_city,
        briefing=("The Great Houses have united against House Atreides. Build a capital of 60,000 people and keep "
                  "it at that size for eight minutes against all four - or destroy every one of them."),
        objectives={"Population": 60000, "HoldSeconds": 480},
        win="House Atreides stands alone on Arrakis. The campaign is won.",
        lose="House Atreides has fallen.",
        credits={"Atreides": 26000, "Fremen": 4750, "Sardaukar": 4750, "Ordos": 4750, "Harkonnen": 4750},
        first_attack=5,
        difficulty={"Easy": 1.45, "Medium": 1, "Hard": 1, "Brutal": 1.45},
    ),
]


def main() -> None:
    import argparse
    import dataclasses
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--mission", type=int, choices=range(1, 11),
                        help="write only this mission (a balance variant with --strength/--out)")
    parser.add_argument("--strength", type=float, help="enemy strength multiplier for the variant")
    parser.add_argument("--goals", type=float, help="variant: population and treasury goals (and a hold-out "
                                                     "timer) x this")
    parser.add_argument("--grace", type=float, help="variant: the enemy grace period (FirstEnemyAttack) x this")
    parser.add_argument("--compact", type=int, default=0, help="variant: 1 = the mission's compact layout")
    parser.add_argument("--out", type=Path, help="variant file to write instead of the shipped map")
    args = parser.parse_args()
    if args.mission:
        m = next(x for x in MISSIONS if x.number == args.mission)
        if args.strength is not None:
            m = dataclasses.replace(m, strength=m.strength * args.strength)
        if args.goals is not None:
            scaled = {k: (int(round(v * args.goals / 1000.0)) * 1000 if k in ("Population", "Credits") else v)
                      for k, v in m.objectives.items()}
            m = dataclasses.replace(m, objectives=scaled,
                                    timeout=int(round(m.timeout * args.goals)) if m.timeout else m.timeout)
        if args.compact:
            if m.compact is None:
                parser.error(f"mission {m.number} has no compact layout")
            m = dataclasses.replace(m, build=m.compact, zone_cap={}, army_keep={})
        if args.grace is not None and m.first_attack:
            m = dataclasses.replace(m, first_attack=max(1, int(round(m.first_attack * args.grace))))
        sc = m.build()
        g.check(sc)
        text = render(m, sc)
        g.check_text(sc, text)
        path = args.out or (OUT_DIR / sc.filename)
        path.write_text(text, encoding="utf-8")
        print(f"{path}: mission {m.number} strength {m.strength:g} objectives {m.objectives} "
              f"timeout {m.timeout} first attack {m.first_attack}")
        return
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for m in MISSIONS:
        sc = m.build()
        g.check(sc)  # the layout's own rules (its original name keeps the Blackout exemption)
        text = render(m, sc)
        g.check_text(sc, text)
        path = OUT_DIR / sc.filename
        path.write_text(text, encoding="utf-8")
        print(f"{sc.filename}: {m.title} {sc.size_x}x{sc.size_y}, {len(sc.structures)} structures, "
              f"{len(sc.units)} units, objectives {m.objectives or 'conquest'}")


if __name__ == "__main__":
    main()
