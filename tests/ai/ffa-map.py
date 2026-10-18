#!/usr/bin/env python3
"""Draw an FFA_ARMY_DUMP (tests/ai/ffa-army.inc, BALANCE_FFA_DUMP) as a whole-map PNG.

Terrain (sand, rock, mountain, spice), every building and road tile in its owner's colour,
every mobile ground fighter as a dot (white ring = wave member, black ring = defender), and per
QuantBot house its forward bundle (X), perimeter point (O), assembly anchor (+) and protected
rally (square), each with a circle of the 8 tiles QuantBot counts as standing there.

  ffa-map.py dump-m19.json out.png [--scale 5]
"""
import argparse
import json

from PIL import Image, ImageDraw

TERRAIN = {'s': (222, 196, 140), 'r': (150, 130, 110), 'M': (90, 70, 55), 'c': (230, 150, 70)}
HOUSE = {0: (60, 90, 220), 1: (40, 170, 60), 2: (220, 60, 60), 3: (150, 90, 40), 4: (150, 60, 170), 5: (220, 200, 40),
         6: (80, 80, 80)}
NAMES = {0: 'Harkonnen', 1: 'Atreides', 2: 'Ordos', 3: 'Fremen', 4: 'Sardaukar', 5: 'Mercenary'}
ROLE_WAVE, ROLE_DEFENCE = 5, 4   # QuantBot::UnitRole order in ffa-army.inc's dump (Wave, Defence)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dump')
    parser.add_argument('out')
    parser.add_argument('--scale', type=int, default=5)
    args = parser.parse_args()
    data = json.load(open(args.dump))
    s = args.scale
    w, h = data['w'], data['h']
    image = Image.new('RGB', (w * s, h * s + 18 * 5))
    draw = ImageDraw.Draw(image)
    for y, row in enumerate(data['terrain']):
        for x, t in enumerate(row):
            draw.rectangle([x * s, y * s, x * s + s - 1, y * s + s - 1], fill=TERRAIN.get(t, (0, 0, 0)))
    def tint(colour, f):
        return tuple(int(c * f + 255 * (1 - f)) for c in colour)
    for house, x, y, sx, sy, item in data['structures']:
        draw.rectangle([x * s, y * s, (x + sx) * s - 1, (y + sy) * s - 1], fill=tint(HOUSE.get(house, (0, 0, 0)), 0.55),
                       outline=tint(HOUSE.get(house, (0, 0, 0)), 0.9))
    for x, y in data['roads']:
        draw.rectangle([x * s + 1, y * s + 1, x * s + s - 2, y * s + s - 2], fill=(110, 110, 110))
    for house, x, y, item, role, mode, moving in data['units']:
        colour = HOUSE.get(house, (0, 0, 0))
        ring = (255, 255, 255) if role == ROLE_WAVE else (0, 0, 0) if role == ROLE_DEFENCE else colour
        cx, cy = x * s + s // 2, y * s + s // 2
        draw.ellipse([cx - s // 2, cy - s // 2, cx + s // 2, cy + s // 2], fill=colour, outline=ring)
    for info in data['houses']:
        colour = HOUSE.get(info['house'], (0, 0, 0))
        for key, shape in (('bundle', 'x'), ('perimeter', 'o'), ('assembly', '+'), ('protected', 'square')):
            point = info.get(key)
            if not point or point[0] < 0:
                continue
            cx, cy = point[0] * s + s // 2, point[1] * s + s // 2
            r = 8 * s
            if key in ('bundle', 'perimeter'):
                draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(0, 0, 0), width=2)
            m = 3 * s
            if shape == 'x':
                draw.line([cx - m, cy - m, cx + m, cy + m], fill=(0, 0, 0), width=4)
                draw.line([cx - m, cy + m, cx + m, cy - m], fill=(0, 0, 0), width=4)
                draw.line([cx - m, cy - m, cx + m, cy + m], fill=colour, width=2)
                draw.line([cx - m, cy + m, cx + m, cy - m], fill=colour, width=2)
            elif shape == 'o':
                draw.ellipse([cx - m, cy - m, cx + m, cy + m], outline=(0, 0, 0), width=4)
                draw.ellipse([cx - m, cy - m, cx + m, cy + m], outline=colour, width=2)
            elif shape == '+':
                draw.line([cx - m, cy, cx + m, cy], fill=(255, 255, 255), width=3)
                draw.line([cx, cy - m, cx, cy + m], fill=(255, 255, 255), width=3)
            else:
                draw.rectangle([cx - m // 2, cy - m // 2, cx + m // 2, cy + m // 2], outline=(255, 255, 255), width=2)
    y0 = h * s + 2
    draw.text((4, y0), f"minute {data['minute']}  X bundle  O perimeter (8-tile circles)  + assembly  [] protected rally; "
                       "dots = fighters (white ring wave, black ring defence)", fill=(255, 255, 255))
    for i, info in enumerate(data['houses']):
        units = [u for u in data['units'] if u[0] == info['house']]
        draw.text((4, y0 + 16 * (i + 1)), f"{NAMES.get(info['house'], info['house'])}: {len(units)} fighters, "
                  f"garrisoned={info.get('garrisoned')} bundle={info.get('bundle')} perimeter={info.get('perimeter')}",
                  fill=HOUSE.get(info['house'], (255, 255, 255)))
    image.save(args.out)


if __name__ == '__main__':
    main()
