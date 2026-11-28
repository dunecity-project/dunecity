#!/usr/bin/env python3
"""A build update moves the player onto the build's shipped Dune City rules.

The same program is started as three "builds" that differ only in the rules they ship, as
staging builds of one version do (tests/mods/shipped-mod-update-probe.inc):

  old       today's defaults with the Soldier at twice its price, BuildUpToEnemy=0 and no PalaceWorm key
  quantbot  the old ObjectData.ini.default with today's QuantBot Config.ini.default
  current   the build's own data

Case 1, profile "quantbot-only": old, then quantbot. The installed Dune City must follow a change
that touches only the QuantBot defaults; a new Random Skirmish must run it and must not be moved
onto a `ws-<hash>` snapshot of the stale copy.

Case 2, profile "snapshot": old (leaving the old automatic snapshot as the active mod, as older
builds did), then current. New games run the current shipped Soldier price and QuantBot settings;
the old build's save and a game pinned to its revision still load the old revision.

Needs the existing Ninja build and its bundled assets. Runs with dummy SDL drivers in isolated
profiles; logs and the test executable stay in --output-dir.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import (NATIVE_TARGET, native_build_commands, native_build_data_dir,  # noqa: E402
                           native_probe_binary, native_probe_data_dir, native_probe_includes,
                           native_probe_relative)

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-shipped-mod-probe-'))
if out.exists():
    shutil.rmtree(out)   # profiles and data directories of an earlier run are not this run's input
out.mkdir(parents=True)


def shipped_rules(data):
    """Today's shipped defaults and the older ones this test derives from them."""
    config = data / 'config'
    objects = (config / 'ObjectData.ini.default').read_text()
    soldier = re.search(r'(?m)^\[Soldier\]\n(?:(?!\[).*\n)*?Price\s*=\s*(\d+)', objects)
    if not soldier:
        raise RuntimeError('No [Soldier] Price in the shipped ObjectData.ini.default')
    price = int(soldier.group(1))
    old_objects = objects[:soldier.start(1)] + str(price * 2) + objects[soldier.end(1):]
    quantbot = (config / 'QuantBot Config.ini.default').read_text()
    if not re.search(r'(?m)^BuildUpToEnemy=1$', quantbot) or not re.search(r'(?m)^PalaceWorm=', quantbot):
        raise RuntimeError('The shipped QuantBot defaults no longer have BuildUpToEnemy=1 and PalaceWorm; '
                           'pick other keys for this test')
    old_quantbot = re.sub(r'(?m)^BuildUpToEnemy=1$', 'BuildUpToEnemy=0', quantbot)
    old_quantbot = re.sub(r'(?m)^PalaceWorm=.*\n', '', old_quantbot)
    return price, objects, old_objects, quantbot, old_quantbot


def install_build(name, binary, data, objects, quantbot):
    """A copy of the program whose data directory ships the given rules; everything else is the build's."""
    target = out / native_probe_relative(name)
    target.parent.mkdir(parents=True)
    shutil.copy2(binary, target)
    own = native_probe_data_dir(out, name)
    own.mkdir(parents=True)
    for entry in data.iterdir():
        if entry.name != 'config':
            (own / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    shutil.copytree(data / 'config', own / 'config')
    (own / 'config/ObjectData.ini.default').write_text(objects)
    (own / 'config/QuantBot Config.ini.default').write_text(quantbot)
    # Bundled mods may sit beside or above the data directory (ModManager's findBundledModPath).
    for above in ('..', '../..', '../../..'):
        if (own / 'mods').exists():
            break
        if (data / above / 'mods/dunecity/graphics_skins').is_dir():
            link = Path(os.path.normpath(own / above / 'mods'))
            if not link.exists():
                link.symlink_to((data / above / 'mods').resolve(), target_is_directory=True)
            break
    return target


subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
lines = native_build_commands(build)
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main menu entry changed; update the test injection point.')
main = main.replace(needle, 'int menuResult = runShippedModUpdateProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = (main[:position] + ''.join('#include "' + p.as_posix() + '"\n' for p in native_probe_includes(root))
        + '#include "' + str(root / 'tests/mods/shipped-mod-update-probe.inc') + '"\n' + main[position:])
source = out / 'shipped-mod-probe-main.cpp'
source.write_text(main)
cc = shlex.split(next(line for line in lines if ' -c ' in line and '/src/main.cpp' in line))
# Test compilation must not overwrite Ninja's production dependency records.
for option in ('-include', '-MT', '-MF'):
    if option in cc:
        i = cc.index(option)
        del cc[i:i + 2]
for option in ('-MD', '-MMD'):
    if option in cc:
        cc.remove(option)
obj = out / 'shipped-mod-probe-main.o'
cc[cc.index('-o') + 1] = str(obj)
cc[cc.index('-c') + 1] = str(source)
map_path = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'
cc.append('-DPROBE_MAP_PATH="' + str(map_path) + '"')
cc.append('-fno-access-control')  # The Start handler and the game's object data are private.
current_binary = native_probe_binary(build, out, 'shipped-current')
link = shlex.split(next(line for line in lines if ' -o ' + NATIVE_TARGET + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(current_binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out / 'build.log').open('w') as log:
    subprocess.run(cc, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

data = native_build_data_dir(build)
price, objects, old_objects, quantbot, old_quantbot = shipped_rules(data)
binaries = {
    'old': install_build('shipped-old', current_binary, data, old_objects, old_quantbot),
    'quantbot': install_build('shipped-quantbot', current_binary, data, old_objects, quantbot),
    'current': current_binary,
}
failures = []


def run(case, stage, profile, **extra):
    """One start of the game on PROFILE by the STAGE build. Returns its log text, or None if it failed."""
    save = profile / 'save/shipped-mod-probe.dls'
    save.parent.mkdir(parents=True, exist_ok=True)
    if not (profile / 'Dune City.ini').exists():
        # The player's last Random Skirmish choices: a small Dune City map against one computer house.
        (profile / 'Dune City.ini').write_text(
            '[General]\nPlay Intro = false\nDiagnostic Logs = true\n'
            '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            '[Random Skirmish]\nMap Size = 64\nSeed = 4242\nFixed Seed = true\nMode = 1\nMod = dunecity\n'
            'Allies = 0\nEnemies = 1\nEnemy AI = qBotHard\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SHIPPED_MOD_PROBE_STAGE=stage, SHIPPED_MOD_PROBE_SAVE=str(save),
               SHIPPED_MOD_PROBE_OLD_PRICE=str(price * 2), SHIPPED_MOD_PROBE_NEW_PRICE=str(price), **extra)
    logfile = out / f'{case}-{stage}.log'
    with logfile.open('w') as log:
        result = subprocess.run([str(binaries[stage]), '--window', '--showlog'], cwd=out, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=180)
    text = logfile.read_text(errors='replace')
    if result.returncode == 0 and f'SHIPPED_MOD_PROBE_PASS: stage={stage}' in text:
        print(f'PASS {case}: {stage} build')
        return text
    reason = re.findall(r'SHIPPED_MOD_PROBE_FAIL: (.*)', text)
    failures.append(f'{case}: {stage} build: ' + (reason[-1] if reason else f'exit {result.returncode}, see {logfile}'))
    print('FAIL ' + failures[-1])
    return None


# Case 1: a build that changes only the QuantBot defaults.
profile = out / 'profile-quantbot-only'
if run('quantbot-only', 'old', profile):
    run('quantbot-only', 'quantbot', profile)

# Case 2: the profile's active mod is the automatic snapshot of an older shipped Dune City.
profile = out / 'profile-snapshot'
seeded = run('snapshot', 'old', profile, SHIPPED_MOD_PROBE_SNAPSHOT='1')
if seeded:
    revision = re.search(r'SHIPPED_MOD_PROBE_OLD: revision=([0-9a-f]{64})', seeded).group(1)
    if (profile / 'mods/active_mod.txt').read_text().strip() != 'ws-' + revision:
        raise RuntimeError('The fixture profile is not on the old snapshot')
    run('snapshot', 'current', profile, SHIPPED_MOD_PROBE_OLD_REVISION=revision)

subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
if failures:
    print(f'{len(failures)} shipped-mod update check(s) failed. Logs: {out}')
    sys.exit(1)
print(f'Shipped-mod update checks passed (Soldier {price * 2} -> {price}). Logs: {out}')
