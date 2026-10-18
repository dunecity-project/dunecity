#!/usr/bin/env python3
"""QuantBot tech bands (docs/ai-play-styles.md, section 8): per house, the tech levels at which the
set of unit-mix slots it can field changes.

A slot is fieldable at tech level L when its unit is enabled, has a price and a builder, its own
TechLevel <= L, and its builder and every prerequisite structure (recursively, the same per-house
values) are obtainable at L - the rule of QuantBot::fieldableMixSlots(tech). A band is a maximal
run of tech levels with the same fieldable set; it is named by its first level (the
[Unit Ratios Tuned] key <House>_Tech<first>).

    python3 tests/ai/tech-bands.py                       # config/ObjectData.ini.default
    python3 tests/ai/tech-bands.py --object-data mods/Tornie/ObjectData.ini
    python3 tests/ai/tech-bands.py --csv out.csv         # house, band, levels, fieldable slots
"""
import argparse
import csv
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
SLOTS = ('tank', 'siege', 'launcher', 'devastator', 'sonic', 'deviator', 'ornithopter', 'trike', 'raider', 'quad')
UNITS = ('Tank', 'Siege Tank', 'Launcher', 'Devastator', 'Sonic Tank', 'Deviator', 'Ornithopter', 'Trike',
         'Raider Trike', 'Quad')
HOUSES = (('harkonnen', 'H'), ('atreides', 'A'), ('ordos', 'O'), ('fremen', 'F'), ('sardaukar', 'S'), ('mercenary', 'M'))
MAX_TECH = 9


def load(path):
    sections, current = {}, None
    for raw in Path(path).read_text(errors='replace').splitlines():
        line = raw.split('#', 1)[0].split(';', 1)[0].strip() if not raw.lstrip().startswith('[') else raw.strip()
        if not line:
            continue
        match = re.match(r'^\[(.+)\]', line)
        if match:
            current = sections.setdefault(match.group(1).strip().lower(), {})
            continue
        if current is not None and '=' in line:
            key, value = line.split('=', 1)
            current[key.strip().lower()] = value.strip()
    return sections


def value(sections, section, key, letter, default):
    for name in (section, 'default unit' if section in {u.lower() for u in UNITS} else 'default structure'):
        entries = sections.get(name, {})
        for k in (f'{key}({letter})'.lower(), key.lower()):
            if k in entries:
                return entries[k]
    return default


def obtainable(sections, name, letter, tech, seen=()):
    name = name.strip().lower()
    if name == 'construction yard':
        return True
    if name in seen or name not in sections:
        return False
    if value(sections, name, 'Enabled', letter, 'false').lower() != 'true':
        return False
    if int(value(sections, name, 'Price', letter, '0') or 0) <= 0:
        return False
    builder = value(sections, name, 'Builder', letter, 'Invalid')
    if builder.lower() == 'invalid' or int(value(sections, name, 'TechLevel', letter, '-1')) > tech:
        return False
    if not obtainable(sections, builder, letter, tech, seen + (name,)):
        return False
    prerequisites = value(sections, name, 'Prerequisite', letter, '')
    return all(obtainable(sections, p, letter, tech, seen + (name,)) for p in prerequisites.split(',') if p.strip())


def fieldable(sections, letter, tech):
    return tuple(obtainable(sections, unit, letter, tech) for unit in UNITS)


def bands(sections, letter):
    out = []
    for tech in range(1, MAX_TECH + 1):
        slots = fieldable(sections, letter, tech)
        if out and out[-1][2] == slots:
            out[-1][1] = tech
        else:
            out.append([tech, tech, slots])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--object-data', nargs='+', default=[str(root / 'config/ObjectData.ini.default')])
    parser.add_argument('--csv')
    args = parser.parse_args()
    rows = []
    for path in args.object_data:
        sections = load(path)
        print(f'\n{path}\n')
        print('| House | Band (tech levels) | Fieldable slots |')
        print('|---|---|---|')
        for house, letter in HOUSES:
            for first, last, slots in bands(sections, letter):
                names = ', '.join(s for s, f in zip(SLOTS, slots) if f) or '-'
                print(f'| {house} | Tech{first} ({first}-{last}) | {names} |')
                rows.append({'object_data': Path(path).name if 'mods' not in path else path, 'house': house,
                             'band': first, 'last': last, 'fieldable': '/'.join(s for s, f in zip(SLOTS, slots) if f)})
    if args.csv:
        with open(args.csv, 'w', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)


if __name__ == '__main__':
    main()
