"""Build the missing reworked Atreides missions (RWRK_SCENA012..020) from the originals.

Each mission keeps the original Westwood terrain (same seed), the player's start and the
original enemy base, then is reworked in the style of Dmitri Fatkin's RWRK missions:
  * a second enemy house with its own base on free rock away from the player,
  * harvesters for every enemy house, more defenders and spice fields,
  * Fatkin-style AI teams and reinforcement waves, more starting credits.
Placement is validated against the seed terrain (every footprint on rock, no overlaps).
Deterministic: python3 rework_missions.py ORIGINAL_DIR OUT_DIR, where ORIGINAL_DIR holds
SCENA012.INI..SCENA020.INI unpacked from SCENARIO.PAK (docs/reworked-campaign.md).
"""
import math, random, sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from scenario_map import Scen, terrain, validate, SIZES, INFANTRY

LEVEL = {12: 5, 13: 5, 14: 6, 15: 6, 16: 6, 17: 7, 18: 7, 19: 7, 20: 8}
PARTNER = {'Ordos': 'Mercenary', 'Harkonnen': 'Sardaukar'}
PLAYER_CREDITS = {5: 7500, 6: 10000, 7: 12500, 8: 17000}
ENEMY_CREDITS = {5: 700, 6: 1200, 7: 2000, 8: 2500}
ENEMY_MAXUNIT = {5: 24, 6: 24, 7: 22, 8: 20}

# Second-base build order: essentials first, defences last.  Anything that does not fit is skipped.
BASE = {
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
GUARDS = {
    5: {'Ordos': ['Tank', 'Tank', 'Launcher', 'Troopers'], 'Mercenary': ['Tank', 'Tank', 'Tank', 'Launcher', 'Launcher', 'Quad', 'Troopers', 'Tank']},
    6: {'Harkonnen': ['Launcher', 'Siege Tank', 'Tank'], 'Sardaukar': ['Siege Tank', 'Siege Tank', 'Tank', 'Tank', 'Launcher', 'Launcher', 'Troopers', 'Troopers', 'Siege Tank']},
    7: {'Ordos': ['Launcher', 'Siege Tank', 'Deviator'], 'Mercenary': ['Siege Tank', 'Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Tank', 'Troopers', 'Siege Tank']},
    8: {'Harkonnen': ['Launcher', 'Devastator'], 'Ordos': ['Launcher', 'Deviator'],
        'Sardaukar': ['Siege Tank', 'Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Troopers'],
        'Mercenary': ['Siege Tank', 'Siege Tank', 'Launcher', 'Launcher', 'Launcher', 'Tank']},
}
# Fatkin's level 5 missions use five teams a house; his level 8+ missions use three.
TEAMS = {
    5: [('Normal', 'Foot', 2, 4), ('Guard', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 4), ('Normal', 'Tracked', 4, 5), ('Guard', 'Tracked', 2, 3)],
    6: [('Normal', 'Foot', 2, 4), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 4, 5), ('Guard', 'Tracked', 2, 3)],
    7: [('Normal', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 3, 5), ('Guard', 'Tracked', 2, 3)],
    8: [('Normal', 'Foot', 2, 3), ('Normal', 'Wheeled', 2, 3), ('Normal', 'Tracked', 3, 5)],
}
# Enemy waves (house role, unit, minute, repeat).  'A' = original enemy, 'B' = added house.
WAVES = {
    5: [('A', 'Tank', 11, False), ('B', 'Quad', 11, False), ('A', 'Raider Trike', 11, False), ('B', 'Launcher', 12, True),
        ('A', 'Tank', 12, True), ('B', 'Troopers', 12, False), ('A', 'Tank', 16, False), ('A', 'Troopers', 16, False),
        ('B', 'Tank', 18, False), ('B', 'Launcher', 18, False)],
    6: [('A', 'Siege Tank', 10, False), ('B', 'Launcher', 10, False), ('A', 'Troopers', 12, False), ('B', 'Siege Tank', 12, True),
        ('A', 'Launcher', 12, True), ('B', 'Troopers', 14, False), ('A', 'Siege Tank', 17, False), ('B', 'Tank', 18, False),
        ('A', 'Devastator', 20, False), ('B', 'Siege Tank', 20, False)],
    7: [('A', 'Siege Tank', 9, False), ('B', 'Launcher', 9, False), ('A', 'Deviator', 11, True), ('B', 'Siege Tank', 11, True),
        ('A', 'Troopers', 12, False), ('B', 'Troopers', 12, False), ('A', 'Tank', 16, False), ('B', 'Launcher', 16, False),
        ('A', "'Thopter", 14, False), ('B', "'Thopter", 14, False)],
    8: [('Harkonnen', 'Devastator', 8, False), ('Ordos', 'Siege Tank', 7, False), ('Sardaukar', 'Launcher', 8, False),
        ('Mercenary', 'Launcher', 7, False), ('Harkonnen', 'Devastator', 11, True), ('Ordos', 'Deviator', 11, True),
        ('Sardaukar', 'Siege Tank', 12, True), ('Mercenary', 'Launcher', 12, True), ('Ordos', "'Thopter", 10, False),
        ('Sardaukar', "'Thopter", 10, False), ('Mercenary', "'Thopter", 10, False)],
}
PLAYER_WAVES = {
    5: [('Tank', 13), ('Tank', 13), ('Tank', 19), ('Launcher', 19)],
    6: [('Siege Tank', 12), ('Launcher', 12), ('Siege Tank', 18), ('Siege Tank', 18)],
    7: [('Siege Tank', 11), ('Launcher', 11), ('Sonic Tank', 17), ('Siege Tank', 17)],
    8: [('Launcher', 9), ('Siege Tank', 9), ('Siege Tank', 13), ('Sonic Tank', 13)],
}
CHOAM_BONUS = {'Harvester': 3, 'Siege Tank': 2, 'Launcher': 1, 'Tank': 1, 'Quad': 1, 'Carryall': 1}
HOUSE_ORDER = ['Atreides', 'Harkonnen', 'Ordos', 'Sardaukar', 'Mercenary', 'Fremen']


def xy(pos): return pos % 64, pos // 64
def pos(x, y): return y * 64 + x


class Board:
    def __init__(self, sc, ter):
        self.sc, self.ter = sc, ter
        self.x0, self.y0, self.w, self.h = sc.bounds()
        self.building = {}   # tile -> label (non-concrete structures)
        self.concrete = set()
        self.unit_tiles = collections.Counter()
        # A unit loaded onto a spice bloom stays alive but invisible to every team, so its house can
        # never be defeated (RWRK_SCENA013, 2026-11-16 playtest). Keep blooms and fields clear.
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


def rework(n, orig_path, out_path):
    rng = random.Random(1000 + n)
    level = LEVEL[n]
    sc = Scen(orig_path)
    ter = terrain(sc.seed)
    board = Board(sc, ter)
    player = 'Atreides'
    houses_orig = [h for h in sc.secs if h not in ('BASIC', 'MAP', 'UNITS', 'STRUCTURES', 'TEAMS', 'REINFORCEMENTS', 'CHOAM')]
    enemies = [h for h in houses_orig if h != player]
    pcy = next(xy(p) for h, t, p in sc.structures() if h == player and t == 'Const Yard')
    pcy = (pcy[0] + 0.5, pcy[1] + 0.5)

    # Added houses: the original enemy's partner (levels 5-7), Sardaukar and Mercenary at level 8.
    if level == 8:
        added = ['Sardaukar', 'Mercenary']
    else:
        added = [PARTNER[enemies[0]]]
    roles = {'A': enemies[0], 'B': added[0]}

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
        guards = GUARDS[level].get(house, [])
        new_units += place_units(board, house, center, guards, pcy, rng, rmin=2, rmax=9)
        for i, ref in enumerate(refineries[:2]):
            new_units += place_harvesters(board, house, ref, 2 if i == 0 else 1, rng)

    # Spice: keep the original fields, add some near every base and the player.
    centers = [(pcy, 6, 11)]
    for house in enemies + list(bases):
        tiles = [t for t, (h, ty) in board.building.items() if h == house]
        if tiles: centers.append(((sum(t[0] for t in tiles) / len(tiles), sum(t[1] for t in tiles) / len(tiles)), 7, 13))
    fields = add_fields(board, sc.positions('Field'), centers, rng, want=max(15, len(sc.positions('Field')) + 8))

    all_enemies = enemies + list(bases)

    # ---- write the scenario ----
    b = dict(sc.secs['BASIC'])
    out = [f'; Scenario {n} control for house Atreides.',
           '; Reworked for DuneCity from the original Westwood scenario (same terrain and original base),',
           "; following Dmitri Fatkin's RWRK missions: a second enemy house, harvesters, more spice and waves.",
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
    out += house_block(player, PLAYER_CREDITS[level], 'Human', 32, pq)
    for h in all_enemies:
        oc = int(sc.get(h, 'Credits', '0') or 0)
        out += house_block(h, max(oc, ENEMY_CREDITS[level]) if h in enemies else ENEMY_CREDITS[level], 'CPU', ENEMY_MAXUNIT[level])

    choam = collections.OrderedDict(sc.secs.get('CHOAM', []))
    if choam:
        for k, v in CHOAM_BONUS.items():
            if k in choam: choam[k] = str(int(choam[k]) + v)
        out += ['', '[CHOAM]'] + [f'{k}={v}' for k, v in choam.items()]

    out += ['', '[TEAMS]']
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
        if typ in ('Concrete', 'Wall'): gens.append(f'GEN{p}={house},{typ}')
        else: ids.append((house, typ, p))
    out += gens
    for house, typ, p in ids:
        out.append(f'ID{sid:03d}={house},{typ},256,{p}'); sid += 1

    out += ['', '[REINFORCEMENTS]']
    rid = 1
    for role, unit, minute, rep in WAVES[level]:
        h = roles.get(role, role)
        if h not in all_enemies: continue
        out.append(f'{rid}={h},{unit},Enemybase,{minute}' + (',+' if rep else '')); rid += 1
    for unit, minute in PLAYER_WAVES[level]:
        out.append(f'{rid}=Atreides,{unit},Homebase,{minute}'); rid += 1
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
    out_dir.mkdir(parents=True, exist_ok=True)
    bad = 0
    for n in range(12, 21):
        errs = rework(n, orig_dir / f'SCENA{n:03d}.INI', out_dir / f'RWRK_SCENA{n:03d}.INI')
        print(f'RWRK_SCENA{n:03d}: {"OK" if not errs else errs[:6]}')
        bad += bool(errs)
    sys.exit(1 if bad else 0)
