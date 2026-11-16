"""Build the reworked (Remastered) missions that Dmitri Fatkin's RWRK set lacks, from the originals.

Each mission keeps the original Westwood terrain (same seed), the player's start and the
original enemy base, then is reworked in the style of Fatkin's RWRK missions of the same level:
  * from mission 5 a second enemy house with its own base on free rock away from the player
    (the original enemy's partner in this campaign, `CAMPAIGNS`),
  * harvesters for every enemy house, more defenders and spice fields,
  * Fatkin-style AI teams and reinforcement waves, more starting credits.
Missions 2-4 are small 32x32 maps with no room for a second base; as in Fatkin's own level 2
reworks they get stronger defenders, more credits and spice and two enemy waves instead.
Placement is validated against the seed terrain (every footprint on rock, no overlaps, no unit
on a spice bloom). Deterministic:
    python3 rework_missions.py ORIGINAL_DIR OUT_DIR [CAMPAIGN...]
where ORIGINAL_DIR holds SCEN?0??.INI unpacked from SCENARIO.PAK and CAMPAIGN is a scenario
letter in `CAMPAIGNS` (default: all). Fatkin's own missions are never regenerated
(docs/reworked-campaign.md).
"""
import math, random, sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario_map import Scen, terrain, validate, SIZES, INFANTRY

# Per campaign: the player's house, the missions Fatkin did not rework, and the house added beside
# each original enemy (the Atreides pairing is Fatkin's own from his level 5-8 missions).
CAMPAIGNS = {
    'A': dict(player='Atreides', missions=range(12, 21), partner={'Ordos': 'Mercenary', 'Harkonnen': 'Sardaukar'}),
    'H': dict(player='Harkonnen', missions=range(4, 23), partner={'Atreides': 'Sardaukar', 'Ordos': 'Mercenary'}),
    'O': dict(player='Ordos', missions=range(2, 23), partner={'Atreides': 'Mercenary', 'Harkonnen': 'Sardaukar'}),
}
MAIN_HOUSES = ('Atreides', 'Harkonnen', 'Ordos')


def level_of(mission):
    """The campaign's tech level: 1, 2-4, 5-7, 8-10, 11-13, 14-16, 17-19, 20-22 -> 1..8."""
    return (mission + 4) // 3


# None keeps the original value.
PLAYER_CREDITS = {2: None, 3: 2250, 4: 4750, 5: 7500, 6: 10000, 7: 12500, 8: 17000}
PLAYER_MAXUNIT = {2: None, 3: 31, 4: 30, 5: 32, 6: 32, 7: 32, 8: 32}
ENEMY_CREDITS = {2: 300, 3: 450, 4: 600, 5: 700, 6: 1200, 7: 2000, 8: 2500}
ENEMY_MAXUNIT = {2: 25, 3: 26, 4: 25, 5: 24, 6: 24, 7: 22, 8: 20}
# Harvesters for an enemy's first and second refinery.
HARVESTERS = {2: (1,), 3: (1, 1), 4: (2, 1), 5: (2, 1), 6: (2, 1), 7: (2, 1), 8: (2, 1)}

# Second-base build order: essentials first, defences last.  Anything that does not fit is skipped.
# Levels 3 and 4 follow Fatkin's added Mercenary and Sardaukar bases (RWRK_SCENA006, RWRK_SCENA009).
BASE = {
    3: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Barracks', 'Spice Silo', 'Outpost'],
    4: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Heavy Fctry', 'WOR', 'Spice Silo',
        'Outpost', 'Windtrap'],
    5: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Heavy Fctry', 'Spice Silo', 'Outpost',
        'Refinery', 'Windtrap', 'WOR', 'Repair', 'Hi-Tech', 'Turret', 'Turret', 'Turret', 'Turret', 'Turret', 'Turret'],
    6: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Heavy Fctry', 'Spice Silo', 'Outpost',
        'Refinery', 'Windtrap', 'WOR', 'Repair', 'Hi-Tech', 'Heavy Fctry', 'Windtrap',
        'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret', 'Turret', 'Turret'],
    7: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Heavy Fctry', 'Spice Silo', 'Outpost',
        'Refinery', 'Windtrap', 'WOR', 'Repair', 'Hi-Tech', 'IX', 'Heavy Fctry', 'Windtrap',
        'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret'],
    # No Palace: the original Harkonnen Palace is the mission's one superweapon (a second one made
    # mission 20 unwinnable in the 2026-11-16 playtest).
    8: ['Const Yard', 'Windtrap', 'Refinery', 'Windtrap', 'Light Fctry', 'Heavy Fctry', 'Outpost', 'Refinery',
        'Windtrap', 'WOR', 'Repair', 'Hi-Tech', 'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret', 'R-Turret'],
}
# House roles: 'A' and 'A2' are the original Atreides/Harkonnen/Ordos enemies in file order; 'B' and
# 'B2' the Sardaukar and Mercenary houses (original ones first, then the added ones).
# '*special' is the house's own heavy unit and '*trike' its trike (SPECIAL, TRIKE).
GUARDS = {
    2: {'A': ['*trike', '*trike', 'Trooper', 'Quad']},
    3: {'A': ['Quad', 'Quad', 'Trooper'], 'B': ['Quad', 'Quad', 'Quad', '*trike', 'Troopers', 'Quad', '*trike', 'Troopers', 'Quad', 'Troopers']},
    4: {'A': ['Tank', 'Tank', 'Troopers'], 'B': ['Tank', 'Tank', 'Tank', 'Tank', 'Quad', 'Troopers', 'Tank', 'Tank', 'Troopers', 'Tank']},
    5: {'A': ['Tank', 'Tank', 'Launcher', 'Troopers'], 'B': ['Tank', 'Tank', 'Tank', 'Launcher', 'Launcher', 'Quad', 'Troopers', 'Tank']},
    6: {'A': ['Launcher', 'Siege Tank', 'Tank'], 'B': ['Siege Tank', 'Siege Tank', 'Tank', 'Tank', 'Launcher', 'Launcher', 'Troopers', 'Troopers', 'Siege Tank']},
    7: {'A': ['Launcher', 'Siege Tank', '*special'], 'B': ['Siege Tank', 'Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Tank', 'Troopers', 'Siege Tank']},
    8: {'A': ['Launcher', '*special'], 'A2': ['Launcher', '*special'],
        'B': ['Siege Tank', 'Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Troopers'],
        'B2': ['Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Tank']},
}
SPECIAL = {'Atreides': 'Sonic Tank', 'Harkonnen': 'Devastator', 'Ordos': 'Deviator'}
# Harkonnen build no trikes and Atreides no Raiders (config/ObjectData.ini.default, Builder(H)/(A)).
TRIKE = {'Ordos': 'Raider Trike', 'Harkonnen': 'Quad'}
SQUAD = {'Harkonnen': 'Troopers'}
# The player's heavy reinforcement: an Ordos player gets Siege Tanks, not Deviators, which do no damage.
PLAYER_SPECIAL = {'Atreides': 'Sonic Tank', 'Harkonnen': 'Devastator', 'Ordos': 'Siege Tank'}
# Fatkin's level 3-5 missions use five teams a house; his level 8+ missions use three.  Level 2 keeps the
# original teams, as his level 2 reworks do.
TEAMS = {
    2: None,
    3: [('Normal', 'Foot', 2, 3), ('Normal', 'Foot', 2, 4), ('Normal', 'Wheeled', 3, 3), ('Normal', 'Wheeled', 4, 5), ('Staging', 'Wheeled', 1, 2)],
    4: [('Normal', 'Foot', 2, 4), ('Guard', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 4), ('Normal', 'Tracked', 2, 4), ('Normal', 'Tracked', 4, 6)],
    5: [('Normal', 'Foot', 2, 4), ('Guard', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 4), ('Normal', 'Tracked', 4, 5), ('Guard', 'Tracked', 2, 3)],
    6: [('Normal', 'Foot', 2, 4), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 4, 5), ('Guard', 'Tracked', 2, 3)],
    7: [('Normal', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 3, 5), ('Guard', 'Tracked', 2, 3)],
    8: [('Normal', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 3, 5)],
}
# Enemy waves (house role, unit, minute, repeat).  Levels 2-4 follow RWRK_SCENA003, 006 and 009.
WAVES = {
    2: [('A', '*trike', 14, False), ('A', 'Trooper', 16, False)],
    3: [('A', 'Trooper', 7, False), ('A', 'Trooper', 7, False), ('B', 'Trooper', 9, False), ('B', 'Trooper', 9, False),
        ('A', 'Quad', 12, False), ('A', 'Trooper', 12, False), ('B', 'Quad', 14, False), ('B', 'Trooper', 14, False),
        ('A', 'Troopers', 18, False), ('B', 'Troopers', 19, False)],
    4: [('A', 'Tank', 11, True), ('A', 'Quad', 11, True), ('A', 'Troopers', 11, False), ('B', 'Tank', 12, True),
        ('B', 'Quad', 12, True), ('B', 'Troopers', 12, False), ('A', 'Tank', 16, False), ('A', 'Troopers', 16, False),
        ('B', 'Tank', 19, False), ('B', 'Troopers', 19, False)],
    5: [('A', 'Tank', 11, False), ('B', 'Quad', 11, False), ('A', '*trike', 11, False), ('B', 'Launcher', 12, True),
        ('A', 'Tank', 12, True), ('B', 'Troopers', 12, False), ('A', 'Tank', 16, False), ('A', 'Troopers', 16, False),
        ('B', 'Tank', 18, False), ('B', 'Launcher', 18, False)],
    6: [('A', 'Siege Tank', 10, False), ('B', 'Launcher', 10, False), ('A', 'Troopers', 12, False), ('B', 'Siege Tank', 12, True),
        ('A', 'Launcher', 12, True), ('B', 'Troopers', 14, False), ('A', 'Siege Tank', 17, False), ('B', 'Tank', 18, False),
        ('A', '*special', 20, False), ('B', 'Siege Tank', 20, False)],
    7: [('A', 'Siege Tank', 9, False), ('B', 'Launcher', 9, False), ('A', '*special', 11, True), ('B', 'Siege Tank', 11, True),
        ('A', 'Troopers', 12, False), ('B', 'Troopers', 12, False), ('A', 'Tank', 16, False), ('B', 'Launcher', 16, False),
        ('A', "'Thopter", 14, False), ('B', "'Thopter", 14, False)],
    8: [('A', '*special', 8, False), ('A2', 'Siege Tank', 7, False), ('B', 'Launcher', 8, False),
        ('B2', 'Launcher', 7, False), ('A', '*special', 11, True), ('A2', '*special', 11, True),
        ('B', 'Siege Tank', 12, True), ('B2', 'Launcher', 12, True), ('A2', "'Thopter", 10, False),
        ('B', "'Thopter", 10, False), ('B2', "'Thopter", 10, False)],
}
PLAYER_WAVES = {
    2: [],
    3: [('*trike', 8), ('*squad', 8), ('Quad', 13), ('*squad', 13)],
    4: [('Tank', 13), ('Quad', 13), ('Tank', 20), ('Tank', 20)],
    5: [('Tank', 13), ('Tank', 13), ('Tank', 19), ('Launcher', 19)],
    6: [('Siege Tank', 12), ('Launcher', 12), ('Siege Tank', 18), ('Siege Tank', 18)],
    7: [('Siege Tank', 11), ('Launcher', 11), ('*special', 17), ('Siege Tank', 17)],
    8: [('Launcher', 9), ('Siege Tank', 9), ('Siege Tank', 13), ('*special', 13)],
}
CHOAM_BONUS = {'Harvester': 3, 'Siege Tank': 2, 'Launcher': 1, 'Tank': 1, 'Quad': 1, 'Carryall': 1}


def unit_for(house, unit, special=SPECIAL):
    if unit == '*special': return special.get(house, 'Siege Tank')
    if unit == '*trike': return TRIKE.get(house, 'Trike')
    if unit == '*squad': return SQUAD.get(house, 'Infantry')
    return unit


def xy(pos): return pos % 64, pos // 64
def pos(x, y): return y * 64 + x


class Board:
    def __init__(self, sc, ter):
        self.sc, self.ter = sc, ter
        self.x0, self.y0, self.w, self.h = sc.bounds()
        self.building = {}   # tile -> label (non-concrete structures)
        self.concrete = set()
        self.unit_tiles = collections.Counter()
        # A unit loaded onto a spice bloom used to stay alive but invisible to every team, so its house
        # could never be defeated (RWRK_SCENA013, 2026-11-16 playtest; fixed by
        # UnitBase::deployAtMapStart). Keep blooms and fields clear anyway.
        self.reserved = {xy(p) for key in ('Bloom', 'Special', 'Field') for p in sc.positions(key)}
        for house, typ, p in sc.structures():
            self.mark(house, typ, p)
        for house, typ, p, act in sc.units():
            self.unit_tiles[xy(p)] += 1

    def inside(self, x, y): return self.x0 <= x < self.x0 + self.w and self.y0 <= y < self.y0 + self.h

    def mark(self, house, typ, p):
        sx, sy = SIZES.get(typ, (1, 1)); px, py = xy(p)
        for dy in range(sy):
            for dx in range(sx):
                t = (px + dx, py + dy)
                if typ == 'Concrete': self.concrete.add(t)
                else: self.building[t] = (house, typ)

    def footprint_ok(self, typ, x, y, gap=1, allow_touch=()):
        sx, sy = SIZES.get(typ, (1, 1))
        for dy in range(sy):
            for dx in range(sx):
                tx, ty = x + dx, y + dy
                if not self.inside(tx, ty) or self.ter[ty][tx] != 'r' or (tx, ty) in self.building or self.unit_tiles[(tx, ty)] \
                        or (tx, ty) in self.reserved:
                    return False
        for dy in range(-gap, sy + gap):
            for dx in range(-gap, sx + gap):
                t = (x + dx, y + dy)
                if t in self.building and self.building[t][1] not in allow_touch:
                    return False
        return True

    def free_unit_tile(self, x, y, typ):
        if not self.inside(x, y) or (x, y) in self.building or (x, y) in self.reserved: return False
        if typ == 'Harvester' and self.ter[y][x] in 'rm': return False
        if self.ter[y][x] == 'm' and typ not in INFANTRY: return False
        return self.unit_tiles[(x, y)] == 0


def dist(a, b): return math.hypot(a[0] - b[0], a[1] - b[1])


def choose_anchor(board, player, enemy_tiles, rng, min_player=20, min_enemy=11):
    best = None
    for y in range(board.y0 + 2, board.y0 + board.h - 2):
        for x in range(board.x0 + 2, board.x0 + board.w - 2):
            if board.ter[y][x] != 'r' or (x, y) in board.building: continue
            dp = dist((x, y), player)
            if dp < min_player: continue
            de = min((dist((x, y), e) for e in enemy_tiles), default=99)
            if de < min_enemy: continue
            free = sum(1 for yy in range(y - 6, y + 7) for xx in range(x - 6, x + 7)
                       if board.inside(xx, yy) and board.ter[yy][xx] == 'r' and (xx, yy) not in board.building)
            score = min(free, 110) + min(dp, 48) * 1.6 + min(de, 25) * 0.3 + rng.random() * 0.01
            if best is None or score > best[0]: best = (score, (x, y), free)
    return best


def place_base(board, house, anchor, order, player):
    placed = []
    for typ in order:
        sx, sy = SIZES[typ]
        cands = []
        is_turret = typ in ('Turret', 'R-Turret')
        for y in range(anchor[1] - 12, anchor[1] + 13):
            for x in range(anchor[0] - 12, anchor[0] + 13):
                if is_turret:
                    if not board.footprint_ok(typ, x, y, gap=1, allow_touch=('Turret', 'R-Turret', 'Wall')): continue
                    da = dist((x, y), anchor)
                    if da > 10: continue
                    # Turrets sit on the base edge facing the player.
                    score = dist((x, y), player) * 1.0 + abs(da - 6) * 1.5
                else:
                    if not board.footprint_ok(typ, x, y): continue
                    c = (x + (sx - 1) / 2, y + (sy - 1) / 2)
                    score = dist(c, anchor) + (0.4 * dist(c, player) if typ == 'Const Yard' else 0) * -0.02
                cands.append((score, x, y))
        if not cands: continue
        cands.sort()
        _, x, y = cands[0]
        board.mark(house, typ, pos(x, y))
        placed.append((typ, pos(x, y)))
    return placed


def pave(board, house, placed, limit=40):
    """Concrete on the free rock tiles directly around the buildings, as in the original bases."""
    tiles = []
    for typ, p in placed:
        if typ in ('Turret', 'R-Turret'): continue
        sx, sy = SIZES[typ]; px, py = xy(p)
        for dy in range(-1, sy + 1):
            for dx in range(-1, sx + 1):
                t = (px + dx, py + dy)
                if board.inside(*t) and board.ter[t[1]][t[0]] == 'r' and t not in board.building and t not in board.concrete:
                    tiles.append(t)
    out = []
    for t in sorted(set(tiles), key=lambda t: (t[1], t[0]))[:limit]:
        board.concrete.add(t); out.append(pos(*t))
    return out


def turret_walls(board, house, placed, player, limit=24):
    """Short wall stubs either side of each turret, never next to a building (keeps lanes open)."""
    out = []
    for typ, p in placed:
        if typ not in ('Turret', 'R-Turret'): continue
        tx, ty = xy(p)
        vx, vy = player[0] - tx, player[1] - ty
        side = [(1, 0), (-1, 0)] if abs(vy) >= abs(vx) else [(0, 1), (0, -1)]
        for dx, dy in side:
            t = (tx + dx, ty + dy)
            if len(out) >= limit: break
            if not board.inside(*t) or board.ter[t[1]][t[0]] != 'r' or t in board.building or board.unit_tiles[t]: continue
            near_building = any((t[0] + ax, t[1] + ay) in board.building and board.building[(t[0] + ax, t[1] + ay)][1] not in ('Turret', 'R-Turret', 'Wall')
                                for ax in (-1, 0, 1) for ay in (-1, 0, 1))
            if near_building: continue
            board.building[t] = (house, 'Wall'); out.append(pos(*t))
    return out


def place_units(board, house, center, types, player, rng, rmin=2, rmax=7, action='Area Guard'):
    out = []
    for typ in types:
        cands = []
        for y in range(center[1] - rmax, center[1] + rmax + 1):
            for x in range(center[0] - rmax, center[0] + rmax + 1):
                d = dist((x, y), center)
                if d < rmin or d > rmax or not board.free_unit_tile(x, y, typ): continue
                # Defenders stand on the player's side of the base.
                cands.append((dist((x, y), player) + rng.random() * 3, x, y))
        if not cands: continue
        cands.sort(); _, x, y = cands[0]
        board.unit_tiles[(x, y)] += 1
        ang = int(round(math.degrees(math.atan2(-(player[1] - y), player[0] - x)) % 360 / 45)) * 32 % 256
        out.append((house, typ, pos(x, y), ang, action))
    return out


def place_harvesters(board, house, refinery_pos, n, rng):
    rx, ry = xy(refinery_pos); out = []
    for _ in range(n):
        cands = []
        for y in range(ry - 9, ry + 10):
            for x in range(rx - 9, rx + 10):
                if not board.free_unit_tile(x, y, 'Harvester'): continue
                d = dist((x, y), (rx + 1, ry + 1))
                if d < 3: continue
                spice = sum(1 for yy in range(y - 2, y + 3) for xx in range(x - 2, x + 3)
                            if board.inside(xx, yy) and board.ter[yy][xx] in 'pP')
                cands.append((d - spice * 0.6 + rng.random(), x, y))
        if not cands: continue
        cands.sort(); _, x, y = cands[0]
        board.unit_tiles[(x, y)] += 1
        out.append((house, 'Harvester', pos(x, y), 64, 'Harvest'))
    return out


def add_fields(board, existing, centers, rng, want):
    fields = list(existing)
    def ok(x, y):
        return board.inside(x, y) and board.ter[y][x] in 'sdpP' and all(dist((x, y), xy(f)) >= 6 for f in fields)
    for c, rmin, rmax in centers:
        for _ in range(200):
            a = rng.random() * 2 * math.pi; r = rmin + rng.random() * (rmax - rmin)
            x, y = int(round(c[0] + math.cos(a) * r)), int(round(c[1] + math.sin(a) * r))
            if ok(x, y): fields.append(pos(x, y)); break
    tries = 0
    while len(fields) < want and tries < 4000:
        tries += 1
        x = rng.randrange(board.x0 + 2, board.x0 + board.w - 2); y = rng.randrange(board.y0 + 2, board.y0 + board.h - 2)
        if ok(x, y): fields.append(pos(x, y))
    return fields


def rework(letter, n, orig_path, out_path):
    campaign = CAMPAIGNS[letter]
    # The Atreides seeds predate the other campaigns; keep them so those missions stay byte-identical.
    rng = random.Random(1000 + n + {'A': 0, 'H': 100, 'O': 200}[letter])
    level = level_of(n)
    sc = Scen(orig_path)
    ter = terrain(sc.seed)
    board = Board(sc, ter)
    player = campaign['player']
    houses_orig = [h for h in sc.secs if h not in ('BASIC', 'MAP', 'UNITS', 'STRUCTURES', 'TEAMS', 'REINFORCEMENTS', 'CHOAM')]
    enemies = [h for h in houses_orig if h != player]
    pcy = next(xy(p) for h, t, p in sc.structures() if h == player and t == 'Const Yard')
    pcy = (pcy[0] + 0.5, pcy[1] + 0.5)

    # Added houses: none on the small level 2 maps, the original enemy's partner (levels 3-7),
    # Sardaukar and Mercenary at level 8 (when the mission has not got them already).
    main = [h for h in enemies if h in MAIN_HOUSES]
    if level == 2:
        added = []
    elif level == 8:
        added = [h for h in ('Sardaukar', 'Mercenary') if h not in enemies]
    else:
        added = [campaign['partner'][main[0]]]
    others = [h for h in enemies if h not in MAIN_HOUSES] + added
    roles = dict(zip(('A', 'A2'), main))
    roles.update(zip(('B', 'B2'), others))
    role_of = {h: r for r, h in roles.items()}

    new_structs, new_units = [], []
    enemy_tiles = [t for t, (h, ty) in board.building.items() if h != player]
    bases = {}
    for house in added:
        best = None
        for min_player in (30, 27, 24):
            best = choose_anchor(board, pcy, enemy_tiles, rng, min_player=min_player, min_enemy=11 if level < 8 else 9)
            if best and best[2] >= 60: break
        if not best or best[2] < 45:
            print(f'  {n}: no room for a {house} base (best={best})'); continue
        anchor = best[1]
        placed = place_base(board, house, anchor, BASE[level], pcy)
        concrete = pave(board, house, placed)
        walls = turret_walls(board, house, placed, pcy)
        for p in concrete: new_structs.append((house, 'Concrete', p))
        for p in walls: new_structs.append((house, 'Wall', p))
        for typ, p in placed: new_structs.append((house, typ, p))
        enemy_tiles += [t for t, (h, ty) in board.building.items() if h == house]
        bases[house] = (anchor, placed)
        print(f'  {n}: {house} base at {anchor} free={best[2]} placed={len(placed)}/{len(BASE[level])} '
              f'({", ".join(t for t, _ in placed)})')

    # Defenders and harvesters.
    for house in enemies + list(bases):
        refineries = [p for h, t, p in sc.structures() if h == house and t == 'Refinery'] + \
                     [p for t, p in bases.get(house, (None, []))[1] if t == 'Refinery']
        if house in bases:
            center = bases[house][0]
        else:
            tiles = [t for t, (h, ty) in board.building.items() if h == house]
            if not tiles: continue
            center = (sum(t[0] for t in tiles) // len(tiles), sum(t[1] for t in tiles) // len(tiles))
        guards = [unit_for(house, u) for u in GUARDS[level].get(role_of.get(house), [])]
        new_units += place_units(board, house, center, guards, pcy, rng, rmin=2, rmax=9)
        for count, ref in zip(HARVESTERS[level], refineries):
            new_units += place_harvesters(board, house, ref, count, rng)

    # Spice: keep the original fields, add some near every base and the player.
    centers = [(pcy, 6, 11)]
    for house in enemies + list(bases):
        tiles = [t for t, (h, ty) in board.building.items() if h == house]
        if tiles: centers.append(((sum(t[0] for t in tiles) / len(tiles), sum(t[1] for t in tiles) / len(tiles)), 7, 13))
    original_fields = sc.positions('Field')
    want = max(4, len(original_fields) + 3) if level == 2 else max(15, len(original_fields) + 8)
    fields = add_fields(board, original_fields, centers, rng, want=want)

    all_enemies = enemies + list(bases)

    # ---- write the scenario ----
    b = dict(sc.secs['BASIC'])
    style = 'stronger defenders, more spice and enemy waves' if level == 2 else \
        'a second enemy house, harvesters, more spice and waves'
    out = [f'; Scenario {n} control for house {player}.',
           '; Reworked for DuneCity from the original Westwood scenario (same terrain and original base),',
           f"; following Dmitri Fatkin's RWRK missions: {style}.",
           '', '[BASIC]']
    for k in ('LosePicture', 'WinPicture', 'BriefPicture', 'TimeOut', 'MapScale', 'CursorPos', 'TacticalPos', 'LoseFlags', 'WinFlags'):
        if k in b: out.append(f'{k}={b[k]}')
    out.append('Author="DuneCity"')
    out += ['', '[MAP]', f'Seed={sc.seed}', 'Field=' + ','.join(str(f) for f in fields)]
    if sc.get('MAP', 'Bloom'): out.append('Bloom=' + sc.get('MAP', 'Bloom'))
    if sc.get('MAP', 'Special'): out.append('Special=' + sc.get('MAP', 'Special'))

    def house_block(h, credits, brain, maxunit, quota=0):
        return ['', f'[{h}]', f'Quota={quota}', f'Credits={credits}', f'Brain={brain}', f'MaxUnit={maxunit}']
    pq = int(sc.get(player, 'Quota', '0') or 0)
    pc = PLAYER_CREDITS[level] or int(sc.get(player, 'Credits', '0') or 0)
    pm = PLAYER_MAXUNIT[level] or int(sc.get(player, 'MaxUnit', '25') or 25)
    out += house_block(player, pc, 'Human', pm, pq)
    for h in all_enemies:
        oc = int(sc.get(h, 'Credits', '0') or 0)
        out += house_block(h, max(oc, ENEMY_CREDITS[level]) if h in enemies else ENEMY_CREDITS[level], 'CPU', ENEMY_MAXUNIT[level])

    choam = collections.OrderedDict(sc.secs.get('CHOAM', []))
    if choam:
        for k, v in CHOAM_BONUS.items():
            if k in choam: choam[k] = str(int(choam[k]) + v)
        out += ['', '[CHOAM]'] + [f'{k}={v}' for k, v in choam.items()]

    out += ['', '[TEAMS]']
    if TEAMS[level] is None:
        out += [f'{k}={v}' for k, v in sc.secs.get('TEAMS', [])]
    else:
        tid = 1
        for h in all_enemies:
            for beh, typ, mn, mx in TEAMS[level]:
                out.append(f'{tid}={h},{beh},{typ},{mn},{mx}'); tid += 1

    out += ['', '[UNITS]']
    uid = 0
    x0, y0, w, hh = sc.bounds()
    for house, typ, p, act in sc.units():
        x, y = xy(p)
        if not (x0 <= x < x0 + w and y0 <= y < y0 + hh): continue
        raw = next(v for k, v in sc.secs['UNITS'] if v.split(',')[3].strip() == str(p) and v.split(',')[1].strip() == typ and v.split(',')[0].strip() == house)
        out.append(f'ID{uid:03d}={raw}'); uid += 1
    for house, typ, p, ang, act in new_units:
        out.append(f'ID{uid:03d}={house},{typ},256,{p},{ang},{act}'); uid += 1

    out += ['', '[STRUCTURES]']
    sid = 0
    gens, ids = [], []
    for house, typ, p in sc.structures() + new_structs:
        x, y = xy(p)
        if not (x0 <= x < x0 + w and y0 <= y < y0 + hh): continue
        if typ in ('Concrete', 'Wall'): gens.append((house, typ, p))
        else: ids.append((house, typ, p))
    # The game drops a structure that overhangs the map or lands on another one (INIMapLoader), as in
    # the originals SCENH015/SCENO015 (a Hi-Tech over the east edge) and SCENO005 (buildings listed
    # twice). Leave those out so every listed structure is one the mission really has.
    taken = set()
    def fits(house, typ, p):
        sx, sy = SIZES.get(typ, (1, 1)); px, py = xy(p)
        tiles = [(px + dx, py + dy) for dy in range(sy) for dx in range(sx)]
        if typ != 'Concrete' and any(not (x0 <= tx < x0 + w and y0 <= ty < y0 + hh) or t in taken
                                     for t in tiles for tx, ty in [t]):
            print(f'  {n}: dropped {house} {typ}@{p} (outside the map or on another structure, as in the original)')
            return False
        if typ != 'Concrete': taken.update(tiles)
        return True
    out += [f'GEN{p}={house},{typ}' for house, typ, p in gens if fits(house, typ, p)]
    for house, typ, p in ids:
        if not fits(house, typ, p): continue
        out.append(f'ID{sid:03d}={house},{typ},256,{p}'); sid += 1

    out += ['', '[REINFORCEMENTS]']
    rid = 1
    for role, unit, minute, rep in WAVES[level]:
        h = roles.get(role)
        if h not in all_enemies: continue
        out.append(f'{rid}={h},{unit_for(h, unit)},Enemybase,{minute}' + (',+' if rep else '')); rid += 1
    for unit, minute in PLAYER_WAVES[level]:
        out.append(f'{rid}={player},{unit_for(player, unit, PLAYER_SPECIAL)},Homebase,{minute}'); rid += 1
    # The original Sardaukar strike on the palace missions stays.
    for k, v in sc.secs.get('REINFORCEMENTS', []):
        if v.startswith('Sardaukar,') and 'Sardaukar' not in all_enemies:
            out.append(f'{rid}={v}'); rid += 1
    out.append('')
    Path(out_path).write_text('\n'.join(out), encoding='latin-1')
    errs = validate(Scen(out_path), ter)
    return errs


if __name__ == '__main__':
    orig_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    letters = sys.argv[3:] or list(CAMPAIGNS)
    out_dir.mkdir(parents=True, exist_ok=True)
    bad = 0
    for letter in letters:
        for n in CAMPAIGNS[letter]['missions']:
            name = f'SCEN{letter}{n:03d}.INI'
            errs = rework(letter, n, orig_dir / name, out_dir / f'RWRK_{name}')
            print(f'RWRK_{name}: {"OK" if not errs else errs[:6]}')
            bad += bool(errs)
    sys.exit(1 if bad else 0)
