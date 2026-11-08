#!/usr/bin/env python3
"""Fail when a menu frame that stays on the stack for a whole nested match grows too large.

Windows gives the main thread a 1 MiB stack. An offline Custom Game runs the entire match
inside playCustomGame -> CustomGamePlayers::showMenu -> onNext -> startSinglePlayerGame, and
1.0.864 overflowed (0xC00000FD in __chkstk) on Start Game because two of those frames each held
a ~437 KB CustomGamePlayers by value. This reads the MSVC x64 unwind data of the objects in a
Windows build tree and checks each of those frames against a budget.

    python scripts/check-windows-nested-game-stack.py --build-dir build
    python scripts/check-windows-nested-game-stack.py --object path/to/CustomGamePlayers.cpp.obj
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Frames that remain live while a match nested under the menus runs, plus frames entered below
# them while Game::initGame activates and verifies the match's mod: MSVC symbol prefix and object.
NESTED_GAME_FRAMES = {
    '?playCustomGame@@': 'Menu/PlaySetup.cpp.obj',
    '?showMenu@CustomGamePlayers@@': 'Menu/CustomGamePlayers.cpp.obj',
    '?onNext@CustomGamePlayers@@': 'Menu/CustomGamePlayers.cpp.obj',
    '?onNext@CustomGameMenu@@': 'Menu/CustomGameMenu.cpp.obj',
    '?onChildWindowClose@CustomGameMenu@@': 'Menu/CustomGameMenu.cpp.obj',
    '?hostCoopGame@SinglePlayerSkirmishMenu@@': 'Menu/SinglePlayerSkirmishMenu.cpp.obj',
    '?openLobby@CoopCampaignMenu@@': 'Menu/CoopCampaignMenu.cpp.obj',
    '?update@CrossplayMenu@@': 'Menu/CrossplayMenu.cpp.obj',
    '?startSinglePlayerGame@@': 'sand.cpp.obj',
    '?startMultiPlayerGame@@': 'sand.cpp.obj',
    '?reloadAllObjectGraphicsForActiveMod@GFXManager@@': 'FileClasses/GFXManager.cpp.obj',
    '?sha256File@Dune2RAssetManager@@': 'mod/Dune2RAssetManager.cpp.obj',
}
# Leave room for game initialization and mod graphics loading below the menus.
# This per-frame guard catches oversized locals; it is not a whole-program stack bound.
DEFAULT_BUDGET = 128 * 1024


def find_readobj(explicit):
    if explicit:
        return explicit
    found = shutil.which('llvm-readobj')
    if found:
        return found
    for candidate in Path('C:/Program Files/Microsoft Visual Studio').glob('*/*/VC/Tools/Llvm/x64/bin/llvm-readobj.exe'):
        return str(candidate)
    sys.exit('llvm-readobj not found; pass --readobj')


def frames(readobj, obj):
    """Largest allocation + saved registers + return address for every function in an object."""
    out = subprocess.run([readobj, '--unwind', str(obj)], capture_output=True, text=True, check=True).stdout
    result, name = {}, None
    for line in out.splitlines():
        start = re.search(r'StartAddress: (\S+)', line)
        if start:
            name = start.group(1)
            result.setdefault(name, [0, 0])
            continue
        if name is None:
            continue
        alloc = re.search(r'ALLOC_(?:LARGE|SMALL) size=(\d+)', line)
        if alloc:
            result[name][0] = max(result[name][0], int(alloc.group(1)))
        elif 'PUSH_NONVOL' in line:
            result[name][1] += 1
    return {fn: alloc + 8 * pushes + 8 for fn, (alloc, pushes) in result.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, help='Windows (MSVC x64) CMake build tree')
    parser.add_argument('--object', type=Path, action='append', default=[], help='check only these objects')
    parser.add_argument('--readobj', help='llvm-readobj executable')
    parser.add_argument('--budget', type=int, default=DEFAULT_BUDGET, help='bytes per frame')
    args = parser.parse_args()
    if not args.build_dir and not args.object:
        parser.error('pass --build-dir or --object')

    readobj = find_readobj(args.readobj)
    if args.object:
        objects = args.object
    else:
        base = args.build_dir / 'src' / 'CMakeFiles' / 'dunecity.dir'
        objects = sorted({base / rel for rel in NESTED_GAME_FRAMES.values()})

    failures, seen = [], set()
    for obj in objects:
        if not obj.is_file():
            failures.append(f'missing object {obj}')
            continue
        matched = False
        for fn, size in frames(readobj, obj).items():
            prefix = next((p for p in NESTED_GAME_FRAMES if fn.startswith(p)), None)
            if prefix is None:
                continue
            matched = True
            seen.add(prefix)
            verdict = 'ok' if size <= args.budget else 'TOO LARGE'
            print(f'{size:9d}  {verdict:9s}  {fn}  ({obj.name})')
            if size > args.budget:
                failures.append(f'{fn}: {size} bytes > {args.budget}')
        if not matched:
            failures.append(f'no nested game frame found in {obj}')
    if not args.object:
        failures += [f'no unwind entry for {p}' for p in NESTED_GAME_FRAMES if p not in seen]
    for failure in failures:
        print('FAIL:', failure)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
