#!/usr/bin/env python3
"""
Structural validator for DuneCity city-sim scenario maps.

It re-derives, from the INI alone, the things the engine will do with the map
and fails on illegal placements or malformed mission data. For authored
scenarios, city-service defects are warnings: they can be the mission problem
and must be assessed in runtime playtests. Strict mode also rejects starvation.
Checks:

  bounds/format   map rows present, right length, only legal terrain chars,
                  linear positions in range, structure names known
  footprints      every structure fits on the map and does not overlap another
  terrain         no structure on mountain/spice/bloom; roads only on rock;
                  zones keep at least one rock tile (CityConstants.h
                  isCityZoneTerrain / isCityBuildableTerrain)
  units           no unit standing on a structure footprint or in a mountain
  road network    connectivity of each owner's road graph; every zone has a
                  road on its 12-tile perimeter ring (TrafficSimulation.cpp
                  findPerimeterRoad)
  traffic         road BFS <= kMaxTrafficDistance (20) from each zone to a
                  road tile 4-adjacent to a complementary-role structure,
                  using the REAL role table from CityEffects.h (Const Yard,
                  Refinery, Silo, Light/Heavy/HighTech Factory, Repair Yard and
                  Starport are industrial; Radar/Outpost, House IX and Airport
                  are commercial; Barracks, WOR and Palace are residential)
  supply          commercial lots see residential AND industrial inside
                  kSupplyRadius = 16 (Chebyshev)
  pollution       residential lots kept >= 6 tiles from industry
                  (kPollutionRadius = 5, so 6 is the first zero-contribution
                  distance)
  police          residential lots inside kPoliceRadius = 23 of a Police
                  Station, Gun Turret or Rocket Turret
  player setup    house sections exist for every owner referenced by the
                  structure/unit/reinforcement lists, exactly one Brain=Human,
                  win/lose flags consistent with the objectives used
  triggers        reinforcement and team lines parse the way INIMapLoader does

Usage:
    python3 scripts/validate_city_maps.py                # all known maps
    python3 scripts/validate_city_maps.py <file.ini> ... # specific files
    python3 scripts/validate_city_maps.py --scenario <file.ini> ...
"""

from __future__ import annotations

import re
import sys
from collections import deque
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

REPO = Path(__file__).resolve().parent.parent
MAPS = REPO / "data" / "maps" / "singleplayer"

# --- engine constants ------------------------------------------------------
MAX_TRAFFIC_DISTANCE = 20     # CityEffects.h kMaxTrafficDistance
SUPPLY_RADIUS = 16            # CityEffects.h kSupplyRadius
POLLUTION_RADIUS = 5          # CityEffects.h kPollutionRadius
POLICE_RADIUS = 23            # CityEffects.h kPoliceRadius

LEGAL_TERRAIN = set("-^~+%@OQgGbrRB")
ROCKISH = set("%")            # rock; slabs do not exist in a fresh INI
ZONE_TERRAIN = set("-^%")     # sand, dunes, rock (isCityZoneTerrain)

FOOTPRINT: Dict[str, Tuple[int, int]] = {
    "Barracks": (2, 2), "Const Yard": (2, 2), "Construction Yard": (2, 2),
    "Gun-Turret": (1, 1), "Turret": (1, 1),
    "Heavy Factory": (3, 2), "Heavy Fctry": (3, 2),
    "Hightech Factory": (3, 2), "Hi-Tech": (3, 2),
    "House IX": (2, 2), "IX": (2, 2),
    "Light Factory": (2, 2), "Light Fctry": (2, 2),
    "Palace": (3, 3), "Outpost": (2, 2), "Radar": (2, 2),
    "Refinery": (3, 2), "Repair Yard": (3, 2), "Repair": (3, 2),
    "Rocket-Turret": (1, 1), "R-Turret": (1, 1),
    "Spice Silo": (2, 2), "Silo": (2, 2),
    "Star Port": (3, 3), "Starport": (3, 3),
    "Concrete": (1, 1), "Slab1": (1, 1), "Slab4": (2, 2),
    "Wall": (1, 1), "Windtrap": (2, 2), "WOR": (2, 2),
    "Nuclear Plant": (3, 3), "Nuclear": (3, 3),
    "Police Station": (2, 2), "Police": (2, 2),
    "Residential Zone": (2, 2), "Zone Residential": (2, 2),
    "Commercial Zone": (2, 2), "Zone Commercial": (2, 2),
    "Industrial Zone": (2, 2), "Zone Industrial": (2, 2),
    "Road": (1, 1), "Power Line": (1, 1), "Stadium": (3, 3), "Airport": (3, 3),
}

# CityEffects.h getStructureCityRole()
ROLE: Dict[str, str] = {}
for _n in ("Residential Zone", "Zone Residential", "Palace", "Barracks", "WOR"):
    ROLE[_n] = "R"
for _n in ("Commercial Zone", "Zone Commercial", "Outpost", "Radar",
           "House IX", "IX", "Airport"):
    ROLE[_n] = "C"
for _n in ("Industrial Zone", "Zone Industrial", "Const Yard", "Construction Yard",
           "Light Factory", "Light Fctry", "Heavy Factory", "Heavy Fctry",
           "Hightech Factory", "Hi-Tech", "Repair Yard", "Repair", "Refinery",
           "Spice Silo", "Silo", "Star Port", "Starport"):
    ROLE[_n] = "I"

ZONE_NAMES = {"Residential Zone": "R", "Zone Residential": "R",
              "Commercial Zone": "C", "Zone Commercial": "C",
              "Industrial Zone": "I", "Zone Industrial": "I"}

# Zone type -> complementary destination role (TrafficSimulation.cpp)
COMPLEMENT = {"R": "C", "C": "I", "I": "R"}

# Everything the engine treats as a road connector for traffic
# (CityConstants.h isTrafficConnector: roads plus Rocket Turrets).
TURRET_CONNECTORS = {"Rocket-Turret", "R-Turret"}

HOUSE_SECTIONS = {"atreides", "harkonnen", "ordos", "fremen", "sardaukar",
                  "mercenary", "custom"}

DROP_LOCATIONS = {"north", "east", "south", "west", "air", "visible",
                  "enemybase", "homebase"}
# sand.cpp getAITeamBehaviorByName / getAITeamTypeByName
TEAM_BEHAVIOURS = {"normal", "guard", "kamikaze", "staging", "flee"}
TEAM_TYPES = {"foot", "wheel", "wheeled", "track", "tracked", "winged",
              "slither", "harvester"}

UNIT_NAMES = {
    "carryall", "chemical carryall", "devastator", "devistator", "deviator",
    "frigate", "harvester", "soldier", "launcher", "mcv", "thopter", "'thopter",
    "thopters", "'thopters", "ornithopter", "quad", "saboteur", "sandworm",
    "siege tank", "sonic tank", "sonictank", "tank", "trike", "raider trike",
    "raider", "trooper", "special", "infantry", "troopers", "rocket trike",
    "sonic trike", "flame tank", "elite launcher", "elite siege tank",
    "chemical siege tank", "harvestank", "rebel harvester",
}


class Report:
    def __init__(self, name: str):
        self.name = name
        self.errors: List[str] = []
        self.warnings: List[str] = []
        self.info: List[str] = []

    def err(self, m: str) -> None:
        self.errors.append(m)

    def warn(self, m: str) -> None:
        self.warnings.append(m)

    def note(self, m: str) -> None:
        self.info.append(m)

    def ok(self) -> bool:
        return not self.errors


# ---------------------------------------------------------------------------
def parse_ini(text: str) -> Dict[str, List[Tuple[str, str]]]:
    out: Dict[str, List[Tuple[str, str]]] = {}
    section = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(';') or line.startswith('#'):
            continue
        m = re.match(r'^\[(.+?)\]$', line)
        if m:
            section = m.group(1).strip().lower()
            out.setdefault(section, [])
            continue
        if '=' in line:
            k, v = line.split('=', 1)
            out.setdefault(section, []).append((k.strip(), v.strip()))
    return out


def get(sec: List[Tuple[str, str]], key: str) -> Optional[str]:
    for k, v in sec:
        if k.lower() == key.lower():
            return v
    return None


# ---------------------------------------------------------------------------
def validate(path: Path, expect: Optional[dict] = None) -> Report:
    expect = expect or {}
    scenario = bool(expect.get("scenario"))
    rep = Report(path.name)
    ini = parse_ini(path.read_text())

    basic = ini.get("basic", [])
    mapsec = ini.get("map", [])
    if not mapsec:
        rep.err("no [MAP] section")
        return rep

    try:
        size_x = int(get(mapsec, "SizeX") or 0)
        size_y = int(get(mapsec, "SizeY") or 0)
    except ValueError:
        rep.err("SizeX/SizeY not integers")
        return rep
    if size_x <= 0 or size_y <= 0:
        rep.err(f"bad map size {size_x}x{size_y}")
        return rep

    rows: List[str] = []
    for y in range(size_y):
        v = get(mapsec, f"{y:03d}")
        if v is None:
            rep.err(f"map row {y} missing")
            rows.append("-" * size_x)
            continue
        if len(v) != size_x:
            rep.err(f"map row {y} is {len(v)} chars, expected {size_x}")
        bad = set(v) - LEGAL_TERRAIN
        if bad:
            rep.err(f"map row {y} has illegal terrain chars {sorted(bad)}")
        rows.append(v.ljust(size_x, '-')[:size_x])

    def terrain(x: int, y: int) -> str:
        if 0 <= x < size_x and 0 <= y < size_y:
            return rows[y][x]
        return '@'

    # -- structures ---------------------------------------------------------
    structures: List[Tuple[str, str, int, int]] = []   # owner, name, x, y
    roads: Dict[Tuple[int, int], str] = {}
    owners: Set[str] = set()

    for key, val in ini.get("structures", []):
        if key.upper().startswith("GEN"):
            try:
                pos = int(key[3:])
            except ValueError:
                rep.err(f"GEN key '{key}' has a non-numeric position")
                continue
            parts = [p.strip() for p in val.split(',')]
            if len(parts) != 2:
                rep.err(f"GEN entry '{key}={val}' must be 'House,Type'")
                continue
            owner, kind = parts
            owners.add(owner.lower())
            if kind not in ("Road", "Wall", "Concrete"):
                rep.err(f"GEN entry '{key}' has unsupported type '{kind}'")
                continue
            x, y = pos % size_x, pos // size_x
            if not (0 <= x < size_x and 0 <= y < size_y):
                rep.err(f"GEN position {pos} outside the map")
                continue
            if kind == "Road":
                if terrain(x, y) not in ROCKISH:
                    rep.err(f"road at ({x},{y}) is on '{terrain(x, y)}', "
                            "but roads only stick to rock/slab")
                if (x, y) in roads:
                    rep.err(f"duplicate road tile at ({x},{y})")
                roads[(x, y)] = owner
            continue

        if not key.upper().startswith("ID"):
            rep.err(f"unsupported [STRUCTURES] key '{key}'")
            continue
        parts = [p.strip() for p in val.split(',')]
        if len(parts) != 4:
            rep.err(f"structure '{key}={val}' must be 'House,Name,Health,Pos'")
            continue
        owner, name, health, pos_s = parts
        owners.add(owner.lower())
        if name not in FOOTPRINT:
            rep.err(f"structure '{key}' has unknown building name '{name}'")
            continue
        try:
            hp = int(health)
            pos = int(pos_s)
        except ValueError:
            rep.err(f"structure '{key}' has non-numeric health/position")
            continue
        if not 0 <= hp <= 256:
            rep.err(f"structure '{key}' health {hp} outside 0..256")
        x, y = pos % size_x, pos // size_x
        if pos < 0 or pos >= size_x * size_y:
            rep.err(f"structure '{key}' position {pos} outside the map")
            continue
        structures.append((owner, name, x, y))

    # footprint / overlap / terrain
    cover: Dict[Tuple[int, int], Tuple[str, str]] = {}
    for owner, name, x, y in structures:
        w, h = FOOTPRINT[name]
        if x + w > size_x or y + h > size_y:
            rep.err(f"{name} at ({x},{y}) runs off the map edge")
            continue
        rock_tiles = 0
        for dy in range(h):
            for dx in range(w):
                t = (x + dx, y + dy)
                if t in cover:
                    other = cover[t]
                    rep.err(f"{name} at ({x},{y}) overlaps {other[1]} on tile {t}")
                cover[t] = (owner, name)
                ch = terrain(*t)
                if ch == '@':
                    rep.err(f"{name} at ({x},{y}) covers mountain tile {t}")
                elif ch in "~+gGrR":
                    rep.err(f"{name} at ({x},{y}) covers spice tile {t}")
                elif ch in "OQbB":
                    rep.err(f"{name} at ({x},{y}) covers a bloom tile {t}")
                if ch in ROCKISH:
                    rock_tiles += 1
                elif name in ZONE_NAMES and ch not in ZONE_TERRAIN:
                    rep.err(f"zone {name} at ({x},{y}) sits on illegal terrain "
                            f"'{ch}' at {t}")
        if name in ZONE_NAMES and rock_tiles == 0:
            rep.err(f"zone {name} at ({x},{y}) has no rock tile to anchor to")
        if name not in ZONE_NAMES and name not in ("Wall",) and rock_tiles == 0:
            rep.warn(f"{name} at ({x},{y}) stands entirely off rock")

    for t in roads:
        if t in cover:
            rep.err(f"road tile {t} is under {cover[t][1]}")

    # -- units --------------------------------------------------------------
    unit_count = 0
    for key, val in ini.get("units", []):
        if not key.upper().startswith("ID"):
            rep.err(f"unsupported [UNITS] key '{key}'")
            continue
        parts = [p.strip() for p in val.split(',')]
        if len(parts) != 6:
            rep.err(f"unit '{key}={val}' must be 'House,Unit,Health,Pos,Angle,Mode'")
            continue
        owner, name, health, pos_s, angle_s, mode = parts
        owners.add(owner.lower())
        if name.lower() not in UNIT_NAMES:
            rep.err(f"unit '{key}' has unknown unit name '{name}'")
        try:
            pos = int(pos_s)
            angle = int(angle_s)
            hp = int(health)
        except ValueError:
            rep.err(f"unit '{key}' has non-numeric fields")
            continue
        if not 0 <= angle <= 255:
            rep.err(f"unit '{key}' angle {angle} outside 0..255")
        if not 0 <= hp <= 256:
            rep.err(f"unit '{key}' health {hp} outside 0..256")
        if pos < 0 or pos >= size_x * size_y:
            rep.err(f"unit '{key}' position {pos} outside the map")
            continue
        x, y = pos % size_x, pos // size_x
        if (x, y) in cover:
            rep.err(f"unit '{key}' at ({x},{y}) spawns inside {cover[(x, y)][1]}")
        if terrain(x, y) == '@' and name.lower() not in (
                "carryall", "'thopter", "thopter", "ornithopter", "frigate",
                "chemical carryall"):
            rep.err(f"ground unit '{key}' at ({x},{y}) spawns on mountain")
        unit_count += 1

    # -- road network & traffic --------------------------------------------
    road_set = set(roads)

    def connectivity(owner: str) -> Tuple[int, int]:
        tiles = {t for t, o in roads.items() if o.lower() == owner}
        if not tiles:
            return 0, 0
        seen = set()
        comps = 0
        biggest = 0
        for start in tiles:
            if start in seen:
                continue
            comps += 1
            q = deque([start])
            seen.add(start)
            n = 0
            while q:
                cx, cy = q.popleft()
                n += 1
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nt = (cx + dx, cy + dy)
                    if nt in tiles and nt not in seen:
                        seen.add(nt)
                        q.append(nt)
            biggest = max(biggest, n)
        return comps, biggest

    # role destination lookup: road tile -> set of roles reachable by stepping
    # off the road onto an adjacent structure
    road_role: Dict[Tuple[int, int], Set[str]] = {}
    for (rx, ry) in road_set:
        got: Set[str] = set()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            hit = cover.get((rx + dx, ry + dy))
            if hit and hit[1] in ROLE:
                got.add(ROLE[hit[1]])
        if got:
            road_role[(rx, ry)] = got

    # Rocket turrets also act as traffic connectors (isTrafficConnector).
    connector = set(road_set)
    for owner, name, x, y in structures:
        if name in TURRET_CONNECTORS:
            connector.add((x, y))

    PERIM = [(-1, -1), (0, -1), (1, -1), (2, -1),
             (-1, 2), (0, 2), (1, 2), (2, 2),
             (-1, 0), (2, 0), (-1, 1), (2, 1)]

    zones = [(o, n, x, y) for o, n, x, y in structures if n in ZONE_NAMES]
    no_frontage: List[Tuple[str, int, int]] = []
    no_dest: List[Tuple[str, int, int]] = []
    for owner, name, x, y in zones:
        zt = ZONE_NAMES[name]
        start = None
        for dx, dy in PERIM:
            if (x + dx, y + dy) in connector:
                start = (x + dx, y + dy)
                break
        if start is None:
            no_frontage.append((zt, x, y))
            continue
        want = COMPLEMENT[zt]
        # BFS over the road network, max MAX_TRAFFIC_DISTANCE steps
        q = deque([(start, 0)])
        seen = {start}
        found = False
        while q:
            (cx, cy), d = q.popleft()
            if want in road_role.get((cx, cy), ()):
                found = True
                break
            if d >= MAX_TRAFFIC_DISTANCE:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nt = (cx + dx, cy + dy)
                if nt in connector and nt not in seen:
                    seen.add(nt)
                    q.append((nt, d + 1))
        if not found:
            no_dest.append((zt, x, y))

    # -- supply / pollution / police ---------------------------------------
    role_pts: Dict[str, List[Tuple[int, int]]] = {"R": [], "C": [], "I": []}
    for owner, name, x, y in structures:
        r = ROLE.get(name)
        if r:
            w, h = FOOTPRINT[name]
            role_pts[r].append((x + w // 2, y + h // 2))
    # CityEffects.h getPollutionEmission exempts these industrial destinations.
    clean_industry = {"Const Yard", "Construction Yard", "Spice Silo", "Silo", "Star Port", "Starport"}
    polluting_pts = [(x + FOOTPRINT[n][0] // 2, y + FOOTPRINT[n][1] // 2)
                     for _o, n, x, y in structures if ROLE.get(n) == "I" and n not in clean_industry]
    police_pts = [(x + 1, y + 1) for _o, n, x, y in structures
                  if n in ("Police Station", "Police")]
    police_pts += [(x, y) for _o, n, x, y in structures
                   if n in ("Rocket-Turret", "R-Turret", "Gun-Turret", "Turret")]

    def near(pt: Tuple[int, int], pts: Sequence[Tuple[int, int]], r: int) -> bool:
        px, py = pt
        for qx, qy in pts:
            if max(abs(px - qx), abs(py - qy)) <= r:
                return True
        return False

    starved_com = []
    polluted_res = []
    unpoliced_res = []
    for owner, name, x, y in zones:
        zt = ZONE_NAMES[name]
        c = (x, y)
        if zt == "C":
            if not near(c, role_pts["R"], SUPPLY_RADIUS) or \
               not near(c, role_pts["I"], SUPPLY_RADIUS):
                starved_com.append(c)
        if zt == "R":
            # Compare lot centres: a residential centre inside kPollutionRadius
            # of an industrial centre takes a non-zero pollution stamp.
            c = (x + 1, y + 1)
            if near(c, polluting_pts, POLLUTION_RADIUS):
                polluted_res.append(c)
            if not near(c, police_pts, POLICE_RADIUS):
                unpoliced_res.append(c)

    # -- houses / objectives / triggers ------------------------------------
    declared = {s for s in ini if s in HOUSE_SECTIONS or re.fullmatch(r'player\d+', s)}
    for o in sorted(owners):
        if o in declared:
            continue
        if o in HOUSE_SECTIONS:
            # INIMapLoader::getHouseID falls back to getHouseByName, and
            # getOrCreateHouse builds a neutral house for it — legal, this is
            # how the sandworm packs are owned.
            rep.note(f"owner '{o}' has no section; the loader creates a "
                     "neutral house for it")
        else:
            rep.err(f"owner '{o}' is used on the map but has no [{o}] section — "
                    "INIMapLoader resolves it to HOUSE_INVALID and drops its "
                    "units/structures")
    human_slots = [s for s in declared
                   if (get(ini[s], "Brain") or "").strip().lower() == "human"]
    if len(human_slots) > 1:
        rep.err(f"more than one Brain=Human section: {sorted(human_slots)}")
    if scenario and human_slots != ["atreides"]:
        rep.err("Single-player city missions require exactly one Atreides Human slot")
    if not human_slots:
        rep.warn("no Brain=Human section — the lobby will pick the slot at random")

    try:
        win_flags = int(get(basic, "WinFlags") or 3)
        lose_flags = int(get(basic, "LoseFlags") or 1)
        timeout = int(get(basic, "TimeOut") or 0)
        version = int(get(basic, "Version") or 1)
    except ValueError:
        rep.err("WinFlags/LoseFlags/TimeOut/Version must be integers")
        win_flags = lose_flags = timeout = 0
        version = 2
    if version != 2:
        rep.err(f"Version={version}: these scenarios need the v2 tile-row format")
    if win_flags & 0x08 and timeout <= 0:
        rep.err("WinFlags has the TIMEOUT bit but TimeOut is 0 — no trigger is created")
    if timeout > 0 and not (win_flags & 0x08):
        rep.warn("TimeOut is set but WinFlags lacks the TIMEOUT bit; it will be ignored")
    if win_flags & 0x04:
        quota_set = any(int(get(ini[s], "Quota") or 0) > 0 for s in declared)
        if not quota_set:
            rep.err("WinFlags has the QUOTA bit but no house declares a Quota")

    for key, val in ini.get("reinforcements", []):
        parts = [p.strip() for p in val.split(',')]
        if len(parts) not in (4, 5):
            rep.err(f"reinforcement '{key}={val}' must have 4 or 5 fields")
            continue
        house, unit, drop, when = parts[:4]
        owners.add(house.lower())
        if house.lower() not in declared:
            rep.err(f"reinforcement '{key}' names undeclared house '{house}'")
        if unit.lower() not in UNIT_NAMES:
            rep.err(f"reinforcement '{key}' names unknown unit '{unit}'")
        if drop.lower() not in DROP_LOCATIONS:
            rep.err(f"reinforcement '{key}' has unknown drop location '{drop}'")
        if not re.fullmatch(r'\d+\+?', when):
            rep.err(f"reinforcement '{key}' has bad time '{when}'")
        if len(parts) == 5 and parts[4] != '+':
            rep.err(f"reinforcement '{key}' 5th field must be '+'")

    for key, val in ini.get("teams", []):
        parts = [p.strip() for p in val.split(',')]
        if len(parts) != 5:
            rep.err(f"team '{key}={val}' must have 5 fields")
            continue
        house, behav, ttype, lo, hi = parts
        if house.lower() not in declared:
            rep.err(f"team '{key}' names undeclared house '{house}'")
        if behav.lower() not in TEAM_BEHAVIOURS:
            rep.err(f"team '{key}' has unknown behaviour '{behav}'")
        if ttype.lower() not in TEAM_TYPES:
            rep.err(f"team '{key}' has unknown type '{ttype}'")
        if not (lo.isdigit() and hi.isdigit()):
            rep.err(f"team '{key}' min/max must be integers")

    if scenario:
        # City problems are intentional scenario content; illegal placements and
        # malformed mission data remain fatal. Growth and outbreaks need runtime proof.
        rep.note("Scenario city-service defects are reported for playtesting, not erased by the generator")
        human_structures = sum(o.lower() == "atreides" for o, n, x, y in structures)
        enemy_structures = len(structures) - human_structures
        rep.note(f"starting structures: Atreides {human_structures}, other houses {enemy_structures}")
        player_items = {n for o, n, x, y in structures if o.lower() == "atreides"}
        has_mcv = any(len(parts) >= 2 and parts[0] == "atreides" and parts[1] == "mcv"
                      for parts in ([p.strip().lower() for p in v.split(',')]
                                    for k, v in ini.get("units", [])))
        if not player_items.intersection({"Const Yard", "Construction Yard"}) and not has_mcv:
            rep.err("Player has neither a Construction Yard nor an MCV")
        if not win_flags & 2:
            rep.err("Mission must detect the player's defeat")

    # -- summary ------------------------------------------------------------
    counts = {"R": 0, "C": 0, "I": 0}
    for _o, n, _x, _y in zones:
        counts[ZONE_NAMES[n]] += 1
    role_counts = {r: len(role_pts[r]) for r in "RCI"}
    rep.note(f"{size_x}x{size_y}; {len(structures)} structures, {len(road_set)} roads, "
             f"{unit_count} units")
    rep.note(f"zones R/C/I = {counts['R']}/{counts['C']}/{counts['I']}; "
             f"role-bearing buildings R/C/I = "
             f"{role_counts['R']}/{role_counts['C']}/{role_counts['I']}")
    for o in sorted({o.lower() for o in roads.values()}):
        comps, biggest = connectivity(o)
        rep.note(f"road graph [{o}]: {comps} component(s), largest {biggest} tiles")
        limit = expect.get("max_road_components", {}).get(o)
        if limit is not None and comps > limit:
            rep.err(f"road graph [{o}] has {comps} components, expected <= {limit}")

    if no_frontage:
        (rep.warn if scenario else rep.err)(f"{len(no_frontage)} zones have no road on their perimeter ring, "
                f"e.g. {no_frontage[:4]}")
    allowed_nodest = expect.get("allow_no_destination", 0)
    if len(no_dest) > allowed_nodest:
        (rep.warn if scenario else rep.err)(f"{len(no_dest)} zones cannot reach a complementary destination "
                f"within {MAX_TRAFFIC_DISTANCE} road tiles "
                f"(allowed {allowed_nodest}), e.g. {no_dest[:4]}")
    elif no_dest:
        rep.note(f"{len(no_dest)} zones start without a traffic destination "
                 "(intended for this scenario)")

    if starved_com:
        msg = (f"{len(starved_com)} commercial zones lack residential or "
               f"industrial supply within {SUPPLY_RADIUS} tiles, e.g. {starved_com[:4]}")
        (rep.warn if scenario or len(starved_com) <= expect.get("allow_starved_com", 0) else rep.err)(msg)
    if polluted_res:
        msg = (f"{len(polluted_res)} residential zones sit within "
               f"{POLLUTION_RADIUS} tiles of industry, e.g. {polluted_res[:4]}")
        (rep.warn if scenario or len(polluted_res) <= expect.get("allow_polluted_res", 0) else rep.err)(msg)
    if unpoliced_res:
        msg = (f"{len(unpoliced_res)} residential zones are outside every "
               f"police/turret coverage radius ({POLICE_RADIUS}), e.g. {unpoliced_res[:4]}")
        (rep.warn if scenario or len(unpoliced_res) <= expect.get("allow_unpoliced_res", 0) else rep.err)(msg)

    return rep


# ---------------------------------------------------------------------------
# Scenario flaws are mission content; the repaired SimCity map remains strict.
SCENARIO_TARGETS = ['2P - 128x128 - Sihaya Basin.ini',
 '3P - 128x128 - Ash Quarter.ini',
 '3P - 160x96 - Coriolis Gap.ini',
 '3P - 96x96 - Cielago Watch.ini',
 '3P - 128x128 - Hagal Flats.ini',
 '3P - 160x96 - Tuono Crossing.ini',
 '4P - 128x128 - Carthag Vise.ini',
 '3P - 128x128 - Arrakeen Blackout.ini',
 '4P - 144x112 - Shield Wall Rift.ini',
 '3P - 160x128 - Harg Pass Convoy.ini',
 '2P - 128x128 - Habbanya Crescent.ini',
 '3P - 128x128 - Habbanya Narrows.ini']
EXPECTATIONS = {name: {"scenario": True} for name in SCENARIO_TARGETS}
EXPECTATIONS["2P - 192x192 - SimCity.ini"] = {
    "max_road_components": {"player1": 1, "player2": 1},
    "allow_polluted_res": 40,
}
DEFAULT_TARGETS = SCENARIO_TARGETS + ["2P - 192x192 - SimCity.ini"]


def main(argv: List[str]) -> int:
    scenario_mode = "--scenario" in argv
    argv = [a for a in argv if a != "--scenario"]
    if argv:
        paths = [Path(a) for a in argv]
    else:
        paths = [MAPS / n for n in DEFAULT_TARGETS]

    bad = 0
    for p in paths:
        if not p.exists():
            print(f"MISSING  {p}")
            bad += 1
            continue
        expect = {"scenario": True} if scenario_mode else EXPECTATIONS.get(p.name)
        rep = validate(p, expect)
        status = "PASS" if rep.ok() else "FAIL"
        print(f"\n=== {status}  {rep.name}")
        for m in rep.info:
            print(f"    . {m}")
        for m in rep.warnings:
            print(f"    ~ warning: {m}")
        for m in rep.errors:
            print(f"    ! error:   {m}")
        if not rep.ok():
            bad += 1
    print()
    print("all maps validated" if bad == 0 else f"{bad} map(s) failed validation")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
