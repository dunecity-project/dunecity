#!/usr/bin/env python3
"""Hospitals heal infantry (include/dunecity/HospitalCare.h), in the existing native build.

Runs tests/units/hospital-care-probe.inc: the five-bed cap and the overflow to a second Hospital,
the Hospital type lock and its release (healed, re-ordered, killed), Carryall delivery, the
automatic rule and the Hospital Care match rule off, and save/load (the loaded game must play the
next 600 cycles exactly as the uninterrupted one). The whole probe runs twice and must log the same
checks both times.

Outputs in --output-dir: checks.csv and run.log per run.
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
BASE_INI = ('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
            'Fullscreen = false\nPreferred Renderer = software\n[General]\nPlay Intro = false\n'
            'Player Name = Hospital tester\nCustom Game Mod = dunecity\n')


def build_probe(build, out):
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    commands = native_build_commands(build)
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed; update the hospital care runner.')
    main = main.replace(needle, 'int menuResult = runHospitalCareProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = (main[:position] + NATIVE_INCLUDES + '#include "'
            + (root / 'tests/units/hospital-care-probe.inc').as_posix() + '"\n' + main[position:])
    source, obj = out / 'hospital-care-main.cpp', out / 'hospital-care-main.o'
    source.write_text(main, encoding='utf-8')
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')   # the census pass (CitySimulation::runZoneGrowth)
    binary = native_probe_binary(build, out, 'hospital-care-probe')
    link = shlex.split(next(line for line in commands if ' -o ' + NATIVE_TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def play(binary, out, tag, case):
    run = out / tag
    run.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix='profile-' + tag + '-', dir=out))
    (profile / 'Dune City.ini').write_text(BASE_INI, encoding='utf-8')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', DUNECITY_PATH_WORKERS='1', HOSPITAL_CARE_OUT=str(run),
               HOSPITAL_CARE_CASE=case,
               HOSPITAL_CARE_METROPOLIS=str(root / 'data/maps/multiplayer/4P - 192x192 - Metropolis.ini'))
    logfile = run / 'run.log'
    with logfile.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=2400)
    lines = logfile.read_text(errors='replace').splitlines()
    records = [line[line.index('HOSPITAL_CARE_PROBE_'):] for line in lines if 'HOSPITAL_CARE_PROBE_' in line]
    if any('HOSPITAL_CARE_PROBE_FAIL' in line for line in records) or not records or records[-1] != 'HOSPITAL_CARE_PROBE_PASS':
        raise RuntimeError('Hospital care probe %s failed; see %s' % (tag, logfile))
    discard_profile_assets(profile)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--case', default='', help='run one check only (cap, overflow, lock, carryall, auto, bot_quiet, save, ui_repair, walkin, ui_no_hospital, ui_beds, ui_carryall, pacing, metropolis)')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='dunecity-hospital-care-'))
    out.mkdir(parents=True, exist_ok=True)
    binary = build_probe(build, out)
    expected = None
    for repeat in (1, 2):
        records = play(binary, out, 'run-%d' % repeat, args.case)
        if expected is not None and records != expected:
            raise RuntimeError('The hospital care probe was not repeatable; see ' + str(out))
        expected = records
    print('\n'.join(expected))
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    print('Hospital care checks passed. Logs: ' + str(out))


if __name__ == '__main__':
    main()
