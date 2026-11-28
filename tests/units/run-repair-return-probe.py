#!/usr/bin/env python3
"""The Repair Yard "Return units" switch and the Dune City Manual Carryall Drops default.

Every case is a Custom Game started the way the main menu starts one (repair-return-probe.inc),
from a profile holding Stefan's saved game options (tests/ai/game-options/). The switch is clicked
in the real Repair Yard side panel; the trips are flown by the game itself. Each case runs twice
and must log the same events at the same cycles. The "default" case starts the game from four
profiles and checks what the Custom Game screen then offers:

  fresh        no saved Dune City options                          -> Manual Carryall Drops on
  stefan       his saved options (on)                              -> on
  stored_off   a Dune City section saved before the default was
               adopted, holding "off" (his MacBook Air, 2026-11-23) -> on, once; a later off is kept
  chosen_off   "off" saved after the default was adopted            -> off

Uses the existing native build; logs and saves stay in --output-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import NATIVE_TARGET, discard_profile_assets, native_build_commands, native_probe_binary, native_probe_includes  # noqa: E402

NATIVE_INCLUDES = ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
SIZE = 64
CASES = ('on', 'off', 'drove_in', 'save', 'ai', 'rule_off')
STEFAN_OPTIONS = root / 'tests/ai/game-options/saboteur-hunt-on.ini'
OLD_SAVE = root / 'tests/units/fixtures/save-9854-orders.dls'
BASE_INI = ('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
            'Player Name = Repair tester\nCustom Game Mod = dunecity\n')
# The Dune City section as an older release wrote it: every option of that day, no revision key.
STORED_OFF = ('[Game Options dunecity]\nGame Speed = 2\nConcrete Required = false\nFog of War = false\n'
              'Start with Explored Map = true\nInstant Build = false\nOnly One Palace = false\n'
              'Rocket-Turrets Need Power = true\nSandworms Respawn = true\nKilled Sandworms Drop Spice = true\n'
              'Manual Carryall Drops = false\nMaximum Number of Units Override = -1\n'
              'Maximum Number of Harvesters Override = -1\nImmortal Human Player = false\nCity Effects = true\n'
              'Plan Building Sites = true\nShared Yard Queue = true\nTrooper Anti-Air = true\n'
              'Starport Population = true\n')


def stefan_section():
    lines = [line for line in STEFAN_OPTIONS.read_text().splitlines() if line and not line.startswith(';')]
    if lines[0] != '[Game Options]' or 'Manual Carryall Drops = true' not in lines:
        raise RuntimeError('Unexpected shape of ' + str(STEFAN_OPTIONS))
    return '\n'.join(['[Game Options dunecity]'] + lines[1:]) + '\n'


PROFILES = {
    'fresh': ('', True),
    'stefan': (stefan_section(), True),
    'stored_off': (STORED_OFF, True),
    'chosen_off': (STORED_OFF + 'Carryall Drop Default = 1\n', False),
}


def fixture_map():
    """Bare rock, two houses in opposite corners, each with a Repair Yard and a Windtrap."""
    def position(x, y):
        return y * SIZE + x
    rows = ''.join('%03d=%s\n' % (y, '%' * SIZE) for y in range(SIZE))
    return ('[BASIC]\nVersion=2\nWinFlags=3\nLoseFlags=1\nTimeOut=0\n\n[MAP]\nSizeX=%d\nSizeY=%d\n%s\n'
            '[Player1]\nCredits=5000\nQuota=0\n[Player2]\nCredits=5000\nQuota=0\n\n'
            '[STRUCTURES]\nID001=Player1,Repair Yard,256,%d\nID002=Player1,Windtrap,256,%d\n'
            'ID003=Player2,Repair Yard,256,%d\nID004=Player2,Windtrap,256,%d\n'
            % (SIZE, SIZE, rows, position(6, 6), position(2, 2), position(55, 56), position(60, 60)))


def build_probe(build, out):
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    commands = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed; update the repair return runner.')
    main = main.replace(needle, 'int menuResult = runRepairReturnProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/repair-return-probe.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'repair-return-main.cpp', out / 'repair-return-main.o'
    source.write_text(main, encoding='utf-8')
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    # A private diagnostic object must not overwrite Ninja's production depfile/PCH records.
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')   # the panel's button and the command queue
    binary = native_probe_binary(build, out, 'repair-return-probe')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def play(binary, out, map_path, case, tag, options, extra=None):
    run = out / tag
    run.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix='profile-' + tag + '-', dir=out))
    (profile / 'Dune City.ini').write_text(BASE_INI + options, encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1', REPAIR_RETURN_CASE=case,
               REPAIR_RETURN_MAP=str(map_path), REPAIR_RETURN_OUT=str(run), **(extra or {}))
    logfile = run / 'run.log'
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=600)
    records = [line[line.index('REPAIR_RETURN_PROBE_'):] for line in logfile.read_text(errors='replace').splitlines()
               if 'REPAIR_RETURN_PROBE_' in line]
    if (any('REPAIR_RETURN_PROBE_FAIL:' in line for line in records) or not records
            or records[-1] != 'REPAIR_RETURN_PROBE_PASS: ' + case):
        raise RuntimeError('Repair return case %s failed; see %s' % (tag, logfile))
    ini = (profile / 'Dune City.ini').read_text(errors='replace')
    discard_profile_assets(profile)
    return records, ini


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--cases', nargs='+', default=list(CASES) + ['old_save', 'default'])
    args = parser.parse_args()
    build = args.build_dir.resolve()
    out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-repair-return-'))
    out.mkdir(parents=True, exist_ok=True)
    binary = build_probe(build, out)
    map_path = out / '2P - 64x64 - Repair Return.ini'
    map_path.write_text(fixture_map(), encoding='utf-8')
    stefan = stefan_section()
    for case in args.cases:
        if case == 'default':
            for name, (options, expect) in PROFILES.items():
                records, ini = play(binary, out, map_path, case, 'default-' + name, options,
                                    {'REPAIR_RETURN_PROFILE': name, 'REPAIR_RETURN_EXPECT_DROPS': '1' if expect else '0'})
                print('\n'.join(records))
            continue
        extra = {'REPAIR_RETURN_OLD_SAVE': str(OLD_SAVE)} if case == 'old_save' else None
        expected = None
        for repeat in (1, 2):
            records, _ = play(binary, out, map_path, case, '%s-%d' % (case, repeat), stefan, extra)
            if expected is not None and records != expected:
                raise RuntimeError('Repair return case %s was not repeatable; see %s' % (case, out))
            expected = records
        print('\n'.join(expected))
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    print('Repair return cases passed: ' + ', '.join(args.cases) + '. Logs: ' + str(out))


if __name__ == '__main__':
    main()
