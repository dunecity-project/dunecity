"""Parse, validate and render Dune II style 64x64 scenario INIs (seed maps).

The terrain comes from the game's own generator: seedmap.cpp is compiled once against
src/MapSeed.cpp into $DUNECITY_SEEDMAP_DIR (default: the system temp directory).
`python3 scenario_map.py FILE...` validates scenarios; render() draws a preview (needs Pillow).
"""
import os, re, subprocess, collections, sys, tempfile, shutil
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def seed_tool():
    out = Path(os.environ.get('DUNECITY_SEEDMAP_DIR', tempfile.gettempdir())) / 'dunecity-seedmap'
    sources = [HERE / 'seedmap.cpp', ROOT / 'src/MapSeed.cpp']
    if out.exists() and all(out.stat().st_mtime >= s.stat().st_mtime for s in sources):
        return str(out)
    sdl = subprocess.run(['sdl2-config', '--cflags'], capture_output=True, text=True).stdout.split() \
        if shutil.which('sdl2-config') else []
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', f'-I{ROOT / "include"}', *sdl,
                    *map(str, sources), '-o', str(out)], check=True)
    return str(out)
SIZES = {'Concrete':(1,1),'Wall':(1,1),'Turret':(1,1),'R-Turret':(1,1),'Windtrap':(2,2),'Refinery':(3,2),
 'Spice Silo':(2,2),'Outpost':(2,2),'Light Fctry':(2,2),'Heavy Fctry':(3,2),'Hi-Tech':(3,2),'Const Yard':(2,2),
 'Repair':(3,2),'WOR':(2,2),'Barracks':(2,2),'Palace':(3,3),'IX':(2,2),'Starport':(3,3)}
HOUSE_COL = {'Atreides':(40,80,230),'Harkonnen':(220,30,30),'Ordos':(30,190,60),'Sardaukar':(160,40,200),
 'Mercenary':(230,200,40),'Fremen':(160,160,160),'Rebels':(90,90,90)}
TER_COL = {'s':(214,170,110),'d':(200,150,95),'r':(110,95,80),'m':(60,50,40),'p':(225,120,50),'P':(190,80,30)}
INFANTRY = {'Soldier','Infantry','Trooper','Troopers','Saboteur','Sandworm'}

def terrain(seed):
    out = subprocess.run([seed_tool(), str(seed)], capture_output=True, text=True, check=True).stdout.split('\n')
    return [list(r) for r in out[:64]]

class Scen:
    def __init__(self, path):
        self.path = Path(path)
        self.lines = self.path.read_bytes().decode('latin-1').replace('\r','').split('\n')
        self.secs = collections.OrderedDict(); cur=None
        for line in self.lines:
            s=line.strip()
            if not s or s.startswith(';') or s.startswith('#'): continue
            m=re.match(r'\[(.+)\]',s)
            if m: cur=m.group(1); self.secs.setdefault(cur,[]); continue
            if cur and '=' in s:
                k,v=s.split('=',1); self.secs[cur].append((k.strip(),v.strip()))
    def get(self, sec, key, default=None):
        for k,v in self.secs.get(sec,[]):
            if k.lower()==key.lower(): return v
        return default
    @property
    def seed(self): return int(self.get('MAP','Seed'))
    @property
    def scale(self): return int(self.get('BASIC','MapScale','0'))
    def bounds(self):
        return {0:(1,1,62,62),1:(16,16,32,32),2:(11,11,21,21)}.get(self.scale,(1,1,62,62))
    def structures(self):
        res=[]
        for k,v in self.secs.get('STRUCTURES',[]):
            f=[x.strip() for x in v.split(',')]
            if k.upper().startswith('GEN'):
                res.append((f[0],f[1],int(k[3:])))
            else:
                res.append((f[0],f[1],int(f[3])))
        return res
    def units(self):
        res=[]
        for k,v in self.secs.get('UNITS',[]):
            f=[x.strip() for x in v.split(',')]
            res.append((f[0],f[1],int(f[3]),f[5] if len(f)>5 else ''))
        return res
    def positions(self, key):
        v=self.get('MAP',key)
        return [int(x) for x in v.split(',') if x.strip()] if v else []

def validate(sc, ter=None):
    ter = ter or terrain(sc.seed)
    x0,y0,w,h = sc.bounds()
    occ={}; errs=[]
    for house,typ,pos in sc.structures():
        sx,sy = SIZES.get(typ,(1,1)); px,py=pos%64,pos//64
        for dy in range(sy):
            for dx in range(sx):
                x,y=px+dx,py+dy
                if not (x0<=x<x0+w and y0<=y<y0+h): errs.append(f'{house} {typ}@{pos} outside map'); continue
                if ter[y][x] != 'r': errs.append(f'{house} {typ}@{pos} tile ({x},{y}) not rock: {ter[y][x]}')
                if typ=='Concrete': continue
                if (x,y) in occ: errs.append(f'{house} {typ}@{pos} overlaps {occ[(x,y)]}')
                occ[(x,y)]=f'{house} {typ}@{pos}'
    upos=collections.Counter()
    for house,typ,pos,act in sc.units():
        x,y=pos%64,pos//64
        if not (x0<=x<x0+w and y0<=y<y0+h): errs.append(f'unit {house} {typ}@{pos} outside map'); continue
        if (x,y) in occ: errs.append(f'unit {house} {typ}@{pos} on structure {occ[(x,y)]}')
        if ter[y][x]=='m' and typ not in INFANTRY: errs.append(f'unit {house} {typ}@{pos} on mountain')
        if typ=='Sandworm' and ter[y][x] in 'rm': errs.append(f'sandworm @{pos} not on sand')
        upos[pos]+=1
    for p,n in upos.items():
        if n>1:
            # infantry can share up to 5 per tile, vehicles cannot
            pass
    return errs

def render(sc, out, cell=8, ter=None, title=None):
    from PIL import Image, ImageDraw
    ter = ter or terrain(sc.seed)
    x0,y0,w,h = sc.bounds()
    img = Image.new('RGB',(w*cell, h*cell+18),(0,0,0)); d=ImageDraw.Draw(img)
    for y in range(h):
        for x in range(w):
            d.rectangle([x*cell,y*cell,(x+1)*cell-1,(y+1)*cell-1], fill=TER_COL.get(ter[y+y0][x+x0],(255,0,255)))
    for pos in sc.positions('Field'):
        x,y=pos%64-x0,pos//64-y0; d.ellipse([x*cell-2,y*cell-2,x*cell+cell+1,y*cell+cell+1],outline=(255,240,0),width=2)
    for pos in sc.positions('Bloom'):
        x,y=pos%64-x0,pos//64-y0; d.rectangle([x*cell+2,y*cell+2,x*cell+cell-3,y*cell+cell-3],fill=(255,255,255))
    for house,typ,pos in sc.structures():
        sx,sy=SIZES.get(typ,(1,1)); x,y=pos%64-x0,pos//64-y0; c=HOUSE_COL.get(house,(255,0,255))
        if typ=='Concrete': d.rectangle([x*cell+1,y*cell+1,(x+1)*cell-2,(y+1)*cell-2],outline=tuple(v//2+60 for v in c)); continue
        if typ=='Wall': d.rectangle([x*cell,y*cell,(x+1)*cell-1,(y+1)*cell-1],fill=tuple(v//2 for v in c)); continue
        d.rectangle([x*cell,y*cell,(x+sx)*cell-1,(y+sy)*cell-1],fill=c,outline=(0,0,0))
        lab={'Const Yard':'C','Windtrap':'W','Refinery':'R','Spice Silo':'s','Outpost':'O','Light Fctry':'L','Heavy Fctry':'H',
             'Hi-Tech':'T','Repair':'r','WOR':'w','Barracks':'B','Palace':'P','IX':'X','Starport':'S','Turret':'t','R-Turret':'^'}.get(typ,'?')
        d.text((x*cell+1,y*cell-1),lab,fill=(255,255,255))
    for house,typ,pos,act in sc.units():
        x,y=pos%64-x0,pos//64-y0; c=HOUSE_COL.get(house,(255,0,255))
        if typ=='Sandworm': d.ellipse([x*cell,y*cell,(x+1)*cell-1,(y+1)*cell-1],outline=(0,0,0),width=2); continue
        r = 2 if typ in INFANTRY else 3
        d.ellipse([x*cell+cell//2-r,y*cell+cell//2-r,x*cell+cell//2+r,y*cell+cell//2+r],fill=c,outline=(255,255,255))
    d.text((3,h*cell+3), title or sc.path.name, fill=(255,255,255))
    img.save(out)
    return img

if __name__=='__main__':
    for p in sys.argv[1:]:
        sc=Scen(p); e=validate(sc); print(p, 'OK' if not e else f'{len(e)} issues'); [print('  ',x) for x in e[:15]]
