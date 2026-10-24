#!/usr/bin/env python3
"""Game frame-cost benchmark: what one displayed frame of a heavy match costs, per component.

Runs a match headless through run-campaign-balance.py with BALANCE_PERF=1
(tests/performance/game-perf.inc) and appends one JSON line per run to --results:
updateGameState() per cycle, every house's own AI update per cycle (p50/p95/p99/max), city
simulation, units, structures, path search, optional software rendering, and a frame model of
--frame-cycles cycles per displayed frame (10 = the engine's cap at Stefan's 2 ms game speed):
frames over 33/50/100 ms and an estimated frame rate. See docs/game-performance.md.

Scenarios:
  --scenario stefan-map    tests/performance/data/stefan-4p-dunecity-brutal.ini: the map of Stefan's
                           slow 1.0.847 game (4P 160x160 Random 1v3 FFA DuneCity T9, seed 779636253,
                           taken from the Workshop revision his save pins), played from its start
                           with his roster (four QuantBot Brutal, spectated), match seed and game
                           options. Loads on every release since 1.0.833.
  --scenario eight-house   tests/performance/data/eight-house-384: Stefan's fpsstillfucked.dls (1.0.849,
                           "4P - 384x384 - new beginning", eight QuantBot Hard, ~810 units, cycle
                           73,528), continued; the two Workshop revisions it pins are rebuilt from
                           the fixture and this tree (build_eight_house_fixture).
  --save FILE --save-profile DIR
                           continue a saved match (DIR/workshop/revisions supplies the mod and map
                           revisions the save pins, e.g. a copy of the player's profile's).
  --map INI                any other custom map (every slot QuantBot --difficulty, free for all,
                           spectated, with --house's seat).

  game-perf.py --scenario stefan-map --minutes 10 --label mine --results R.jsonl
  game-perf.py --save slow.dls --save-profile PROFILE --minutes 10 --label mine --results R.jsonl
  game-perf.py --source ../tag-wt --build-dir ../tag-wt/build --scenario stefan-map ...   # an older release
  game-perf.py --summary R.jsonl
"""
import argparse
import gzip
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import require_free_space, scratch_dir  # noqa: E402

DATA = root / 'tests/performance/data'
SCENARIOS = {
    # Seed and roster as the save records them (GAME_PERF_SETUP of a --save run).
    'stefan-map': {'map': DATA / 'stefan-4p-dunecity-brutal.ini', 'house': 'atreides', 'seed': 1443930314,
                   'roster': 'harkonnen:1,atreides:2,fremen:3,ordos:4',
                   'options': DATA / 'stefan-game-options.ini', 'difficulty': 'brutal', 'mod': 'dunecity'},
}
CYCLES_PER_MINUTE = 3750   # MILLI2CYCLES(60000) at 16 ms per cycle
EIGHT_HOUSE = DATA / 'eight-house-384'
# Where the 8-house mod revision's other files live in this tree (found by their SHA-256).
EIGHT_HOUSE_SOURCES = ('mods/dunecity/graphics_skins', 'config')
# perf_regression budgets of the 8-house case, as multiples of the calibration loop (see
# docs/game-performance.md "perf_regression: eight houses on 384x384"). The p99 cycle of this
# save is a QuantBot build pass (8 houses x one per 100 cycles = 8% of cycles): 1.0.852 measured
# 2.2-2.9 (mini) and 1.86 (Pro) there, perf/fps-round3 1.1-1.5 (mini), so 1.6 fails 1.0.852.
EIGHT_BUDGETS = {'cpu_p99': 1.6, 'frame_cpu_p99': 5.5, 'mainloop_frame_p99': 8.0}


def build_eight_house_fixture(scratch):
    """The 8-house save and a profile whose Workshop store holds the two revisions it pins (the
    "Dune City" mod 746bae13, the 384x384 map 76deda62), rebuilt from tests/performance/data/
    eight-house-384: the save and both manifests are stored there (gzip), with the three files
    this tree does not have; the mod revision's other files are this tree's dunecity
    graphics skins and config/ ini files. Every file is checked against its manifest hash, so a
    changed skin fails here by name rather than as a load error. Returns (save, profile)."""
    out = scratch / 'eight-house'
    save, profile = out / 'fpsstillfucked.dls', out / 'profile'
    if save.is_file():
        return save, profile
    own = {}
    for path in (EIGHT_HOUSE / 'files').iterdir():
        data = gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes()
        own[hashlib.sha256(data).hexdigest()] = data
    tree = None
    for name, version in json.loads((EIGHT_HOUSE / 'versions.json').read_text()).items():
        packed = EIGHT_HOUSE / (name + '.manifest.gz')
        manifest = gzip.decompress(packed.read_bytes()) if packed.is_file() else (EIGHT_HOUSE / (name + '.manifest')).read_bytes()
        if hashlib.sha256(manifest).hexdigest() != name:
            raise RuntimeError('Fixture manifest %s does not match its name' % name)
        target = profile / 'workshop/revisions' / name
        for line in manifest.decode().splitlines():
            if not line.startswith('file='):
                continue
            digest, _, path = line[5:].split(',', 2)
            data = own.get(digest)
            if data is None:
                if tree is None:
                    tree = {}
                    for base in EIGHT_HOUSE_SOURCES:
                        for candidate in (root / base).rglob('*'):
                            if candidate.is_file():
                                tree.setdefault(hashlib.sha256(candidate.read_bytes()).hexdigest(), candidate)
                if digest not in tree:
                    raise RuntimeError('8-house fixture: no file in %s has the bytes of %s (%s); the mod '
                                       'revision cannot be rebuilt' % (', '.join(EIGHT_HOUSE_SOURCES), bytes.fromhex(path).decode(), digest))
                data = tree[digest].read_bytes()
            destination = target / 'files' / bytes.fromhex(path).decode()
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (target / 'manifest').write_bytes(manifest)
        (target / 'version').write_text(str(version))
    save.write_bytes(gzip.decompress((EIGHT_HOUSE / 'fpsstillfucked.dls.gz').read_bytes()))
    return save, profile


def prepare_foreign_tree(source):
    """Make an older release's probe able to run BALANCE_PERF: copy game-perf.inc in, put a
    BALANCE_PERF branch in front of its ordinary match loop and, where the release predates it,
    apply BALANCE_GAME_OPTIONS as today's probe does. Throwaway tag worktrees only."""
    if source.resolve() == root:
        return
    target = source / 'tests/performance/game-perf.inc'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / 'tests/performance/game-perf.inc', target)
    patch_foreign_runner(source / 'tests/ai/run-campaign-balance.py')
    probe = source / 'tests/ai/campaign-balance-probe.inc'
    text = probe.read_text()
    if 'GamePerf::run' in text:
        return
    loops = re.findall(r'^( *)while \(!game->finished && game->getGameCycleCount\(\) < end\) \{\n', text, re.M)
    if len(loops) != 1:
        raise RuntimeError('Cannot find the ordinary match loop in ' + str(probe))
    indent = loops[0]
    loop = indent + 'while (!game->finished && game->getGameCycleCount() < end) {\n'
    text = text.replace(loop, indent + 'if (std::getenv("BALANCE_PERF")) GamePerf::run(*game, '
                        'static_cast<Uint32>(std::strtoul(std::getenv("BALANCE_PERF_CYCLES"), nullptr, 10)), nullptr);\n'
                        + indent + 'else\n' + loop)
    if 'BALANCE_GAME_OPTIONS' not in text:
        anchor = '    GameInitSettings init(humanHouse, settings.gameOptions, level);\n'
        if text.count(anchor) != 1:
            raise RuntimeError('Cannot find the game setup in ' + str(probe))
        text = text.replace(anchor, '    if (const char* options = std::getenv("BALANCE_GAME_OPTIONS"))\n'
                            '        applyGameOptionsFromConfig(INIFile(options), "Game Options", settings.gameOptions);\n'
                            + anchor)
    text = text.replace('int runCampaignBalanceProbe() {', '#include "../performance/game-perf.inc"\n'
                        '#include <FileClasses/INIFile.h>\nint runCampaignBalanceProbe() {', 1)
    probe.write_text(text)


def patch_foreign_runner(runner):
    """--rendered on an older release: its runner always forced the dummy video driver. With
    GAME_PERF_VIEWPORT=WxH it opens a real (Cocoa) window of that size instead. Runners with
    --real-video (1.0.84x) need nothing."""
    text = runner.read_text()
    if 'GAME_PERF_VIEWPORT' in text or "'--real-video'" in text:
        return
    dummy = "SDL_VIDEODRIVER='dummy',"
    anchor = "\nif args.custom_map:\n    env['BALANCE_CUSTOM_MAP']"
    if text.count(dummy) != 1 or text.count(anchor) != 1:
        raise RuntimeError('Cannot patch the video setup of ' + str(runner))
    text = text.replace(dummy, "SDL_VIDEODRIVER='cocoa' if os.environ.get('GAME_PERF_VIEWPORT') else 'dummy',")
    text = text.replace(anchor, "\nif os.environ.get('GAME_PERF_VIEWPORT'):\n"
                        "    width, height = os.environ['GAME_PERF_VIEWPORT'].split('x')\n"
                        "    (out/'profile').mkdir(parents=True, exist_ok=True)\n"
                        "    (out/'profile'/'Dune City.ini').write_text(f'[Video]\\nPhysical Width = {width}\\n"
                        "Physical Height = {height}\\nWidth = {width}\\nHeight = {height}\\nInterface Height = {height}\\n"
                        "Fullscreen = false\\n[General]\\nPlay Intro = false\\n')" + anchor)
    runner.write_text(text)


def telemetry_windows(profile):
    """Aggregate the decision log's performance_window rows (five wall seconds each): per scope
    and house the count, sum and max in ms, and frames over 33/100 ms."""
    totals = {}
    for events in profile.rglob('events.jsonl'):
        for line in events.open(errors='replace'):
            if '"performance_window"' not in line:
                continue
            for metric in json.loads(line)['data']['metrics'].values():
                if metric['unit'] != 'us' or metric['item'] != -1:
                    continue
                key = metric['scope'] + ('' if metric['house'] < 0 else '/h%d' % metric['house'])
                t = totals.setdefault(key, {'count': 0, 'sum_ms': 0.0, 'max_ms': 0.0, 'over_33ms': 0, 'over_100ms': 0})
                t['count'] += metric['count']; t['sum_ms'] += metric['sum'] / 1000.0
                t['max_ms'] = max(t['max_ms'], metric['max'] / 1000.0)
                t['over_33ms'] += metric['over_33ms']; t['over_100ms'] += metric['over_100ms']
    for t in totals.values():
        t['mean_ms'] = round(t['sum_ms'] / t['count'], 3) if t['count'] else 0
        t['sum_ms'] = round(t['sum_ms'], 1)
    keep = ('frame', 'frame.ai', 'frame.city', 'frame.units', 'frame.structures', 'frame.path', 'frame.render',
            'frame.gap', 'house.update')
    return {k: v for k, v in totals.items() if k.split('/')[0] in keep}


# The real launch path (--menu-launch): the player's Random Skirmish choices as Stefan's slow game had
# them (4P 160x160 DuneCity, three QuantBot Brutal enemies, spectated by a QuantBot Brutal house), with
# a fixed map seed. The metaserver points at a closed local port, so the Workshop outbox pump and
# match analytics run their real code without reaching the public service.
MENU_LAUNCH_INI = """[General]
Play Intro = false
Player Name = perf_regression
[Video]
Physical Width = 1440
Physical Height = 900
Width = 1440
Height = 900
Interface Height = 900
Fullscreen = false
FrameLimit = false
[Network]
MetaServer = http://127.0.0.1:9/metaserver/metaserver.php
[Random Skirmish]
Map Size = 160
Rock = 2
Spice = 2
Symmetric = false
Seed = 223538685
Fixed Seed = true
House = -1
Credits = 3000
Allies = 0
Enemies = 3
Enemies Allied = false
Mode = 1
Mod = dunecity
Spectate = true
Spectate AI = qBotBrutal
Enemy AI = qBotBrutal
"""


def build_menu_launch_probe(build, out):
    """src/main.cpp with the main menu call replaced by tests/performance/menu-launch.inc, linked
    against this build's other objects (the same method as tests/menu/run-menu-probe.py)."""
    subprocess.run([sys.executable, str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed.')
    main = main.replace(needle, 'int menuResult = runMenuLaunchProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = main[:position] + '#include "' + str(root / 'tests/performance/menu-launch.inc') + '"\n' + main[position:]
    source, obj = out / 'menu-launch-main.cpp', out / 'menu-launch-main.o'
    source.write_text(main)
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            index = compile_command.index(option)
            del compile_command[index:index + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')
    app = out / 'menu-launch.app/Contents'
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    if not (app / 'Resources').exists():
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/menu-launch'
    link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def run_menu_launch(args, scratch):
    """Main menu -> Random Skirmish -> Start, then the interactive game for --menu-launch wall seconds
    at the options' 2 ms game speed. Per frame: the decision log's components and 'other' (frame time
    the components do not account for: the app's per-frame work outside simulation and rendering)."""
    out = scratch / 'menu-launch'
    out.mkdir(parents=True, exist_ok=True)
    binary = build_menu_launch_probe(args.build_dir.resolve(), out)
    profile = out / 'profile'
    profile.mkdir(exist_ok=True)
    # His game options, both as the general section and as the dunecity mod's user section.
    options = (DATA / 'stefan-game-options.ini').read_text()
    (profile / 'Dune City.ini').write_text(MENU_LAUNCH_INI + options + options.replace('[Game Options]', '[Game Options dunecity]'))
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_AUDIODRIVER='dummy', DUNECITY_AI_TELEMETRY='1',
               MENU_LAUNCH_SECONDS=str(args.menu_launch),
               SDL_VIDEODRIVER='cocoa' if args.rendered else 'dummy')
    if not args.rendered:
        env['SDL_RENDER_DRIVER'] = 'software'
    if args.path_workers:
        env['DUNECITY_PATH_WORKERS'] = str(args.path_workers)
    started = time.time()
    with (out / 'run.log').open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=args.menu_launch + 600)
    text = (out / 'run.log').read_text(errors='replace')
    found = next((json.loads(line.split('MENU_LAUNCH_RESULT: ', 1)[1]) for line in text.splitlines()
                  if 'MENU_LAUNCH_RESULT: ' in line), None)
    if found is None or 'Random Skirmish: ' not in text or 'Starting game...' not in text:
        raise RuntimeError('The menu launch did not start a Random Skirmish game; see ' + str(out / 'run.log'))
    windows = telemetry_windows(profile)
    cycles = 0
    for events in profile.rglob('events.jsonl'):
        for line in events.open(errors='replace'):
            if '"performance_window"' in line:
                cycles += sum(m['sum'] for m in json.loads(line)['data']['metrics'].values() if m['scope'] == 'frame.cycles')
    frame = windows.get('frame', {'count': 0, 'sum_ms': 0.0})
    if frame['count'] < 100 or cycles == 0:
        raise RuntimeError('The menu-launched game recorded %d frames, %d cycles; see %s' % (frame['count'], cycles, out))
    parts = ('frame.ai', 'frame.city', 'frame.units', 'frame.structures', 'frame.path', 'frame.render')
    per_frame = {k.split('.', 1)[1]: windows.get(k, {'sum_ms': 0.0})['sum_ms'] / frame['count'] for k in parts}
    seconds = frame['sum_ms'] / 1000.0
    result = dict(mode='menu-launch', label=args.label, calibration_ms=found['calibration_ms'], frames=frame['count'],
                  cycles=cycles, fps=frame['count'] / seconds, cycles_per_s=cycles / seconds,
                  frame_ms=frame['sum_ms'] / frame['count'], per_frame_ms=per_frame,
                  other_ms=max(0.0, frame['sum_ms'] / frame['count'] - sum(per_frame.values())),
                  windows=windows, wall_s=round(time.time() - started, 1), host=os.uname().nodename,
                  load=os.getloadavg()[0], time=time.strftime('%Y-%m-%d %H:%M:%S'))
    shutil.rmtree(out, ignore_errors=True)
    return result


def run_one(args, scratch, index):
    source = args.source.resolve()
    prepare_foreign_tree(source)
    scenario = dict(SCENARIOS.get(args.scenario, {}))
    save_profile = args.save_profile
    if args.scenario == 'eight-house' and not (args.save or args.map):
        save, save_profile = build_eight_house_fixture(scratch)
        scenario = {'save': save}
    if args.save: scenario = {'save': args.save}
    if args.map: scenario = {'map': args.map, 'house': args.house, 'seed': args.seed, 'options': args.game_options,
                             'difficulty': args.difficulty, 'mod': args.mod}
    out = scratch / ('run-%d' % index)
    cycles = int(args.minutes * CYCLES_PER_MINUTE)
    command = [sys.executable, str(source / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir.resolve()),
               '--output-dir', str(out), '--wall-timeout', str(args.wall_timeout)]
    if args.prebuilt_probe: command += ['--prebuilt-probe', str(args.prebuilt_probe)]
    if args.path_workers: command += ['--path-workers', str(args.path_workers)]
    options = None
    if 'save' in scenario:
        command += ['--load-save', str(Path(scenario['save']).resolve()), '--mod', args.mod, '--minutes', '120']
        if save_profile: command += ['--save-profile', str(Path(save_profile).resolve())]
    else:
        command += ['--custom-map', str(Path(scenario['map']).resolve()), '--house', scenario['house'],
                    '--local-spectator', '--free-for-all', '--mod', scenario['mod'],
                    '--partner-difficulty', scenario['difficulty'], '--enemy-difficulty', scenario['difficulty'],
                    '--seed', str(scenario['seed']), '--minutes', str(max(1, min(120, int(args.minutes) + 1)))]
        if scenario.get('roster'): command += ['--roster', scenario['roster']]
        if scenario.get('options'): options = Path(scenario['options']).resolve()
    env = dict(os.environ, BALANCE_PERF='1', BALANCE_PERF_CYCLES=str(cycles),
               BALANCE_PERF_FRAME_CYCLES=str(args.frame_cycles), BALANCE_UNIT_PHASE_TIMING='1')
    # The probes read [Game Options] from this file (older releases through prepare_foreign_tree).
    if options: env['BALANCE_GAME_OPTIONS'] = str(options)
    env.setdefault('BALANCE_DIGEST_CYCLES', '750')
    if args.render_every: env['BALANCE_PERF_RENDER_EVERY'] = str(args.render_every)
    if args.csv: env['BALANCE_PERF_CSV'] = str(Path(args.csv).resolve()) + ('' if args.repeat == 1 else '.%d' % index)
    if args.motion_csv:
        env['BALANCE_PERF_MOTION_CSV'] = str(Path(args.motion_csv).resolve()) + ('' if args.repeat == 1 else '.%d' % index)
    if args.dump_map: env['BALANCE_PERF_DUMP_MAP'] = str(Path(args.dump_map).resolve())
    if args.mainloop or args.rendered: env['BALANCE_PERF_MAINLOOP'] = '1'
    if args.rendered and "'--real-video'" in (source / 'tests/ai/run-campaign-balance.py').read_text():
        command += ['--real-video', '--viewport', args.viewport or '1440x900']
    elif args.rendered:
        env['GAME_PERF_VIEWPORT'] = args.viewport or '1440x900'   # patch_foreign_runner
    started = time.time()
    done = subprocess.run(command, cwd=source, env=env, capture_output=True, text=True)
    log = (out / 'run.log').read_text(errors='replace') if (out / 'run.log').is_file() else ''
    if done.returncode != 0:
        sys.stderr.write(done.stdout[-3000:] + done.stderr[-3000:] + log[-3000:])
        raise RuntimeError('Perf run failed (%d); kept %s' % (done.returncode, out))
    if args.mainloop or args.rendered:
        result = next((json.loads(line.split('GAME_PERF_MAINLOOP: ', 1)[1]) for line in log.splitlines()
                       if 'GAME_PERF_MAINLOOP: ' in line), None)
        if result is None:
            raise RuntimeError('No GAME_PERF_MAINLOOP in ' + str(out / 'run.log'))
        result['mode'] = 'rendered' if args.rendered else 'mainloop'
        changed = re.search(r'GAME_PERF_BUDGET_CHANGED: (\S+ \S+)', log)
        result['budget_changed'] = changed.group(1) if changed else None
        result['windows'] = telemetry_windows(out / 'profile')
        perf_log = out / 'profile/DuneCity-Performance.log'
        if perf_log.is_file():
            spikes = [float(m) for m in re.findall(r'\[AI SPIKE\] Cycle \d+ house=\d+ update=([0-9.]+)ms', perf_log.read_text())]
            result['ai_spikes'] = {'count': len(spikes), 'max': max(spikes, default=0),
                                   'mean': sum(spikes) / len(spikes) if spikes else 0}
    else:
        result = next((json.loads(line.split('GAME_PERF_RESULT: ', 1)[1]) for line in log.splitlines()
                       if 'GAME_PERF_RESULT: ' in line), None)
        if result is None:
            raise RuntimeError('No GAME_PERF_RESULT in ' + str(out / 'run.log'))
        result['mode'] = 'stepped'
    result['digests'] = re.findall(r'PROBE_DIGEST: (cycle=\d+ digest=[0-9a-f]+)', log)
    result['setup'] = [line.split('GAME_PERF_SETUP: ', 1)[1] for line in log.splitlines() if 'GAME_PERF_SETUP: ' in line]
    try:
        commit = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=source, text=True).strip()
    except subprocess.CalledProcessError:
        commit = 'unknown'
    result.update(label=args.label, scenario=args.scenario if not (args.save or args.map) else str(args.save or args.map),
                  minutes=args.minutes, source=str(source), commit=commit, wall_s=round(time.time() - started, 1),
                  render_every=args.render_every, path_workers=args.path_workers, host=os.uname().nodename,
                  load=os.getloadavg()[0], time=time.strftime('%Y-%m-%d %H:%M:%S'))
    shutil.rmtree(out, ignore_errors=True)
    return result


def summarize(paths):
    rows = [json.loads(line) for path in paths for line in Path(path).read_text().splitlines() if line.strip()]
    loops = [r for r in rows if r.get('mode') in ('mainloop', 'rendered')]
    rows = [r for r in rows if r.get('mode', 'stepped') == 'stepped']
    if loops:
        # From the decision log's five-second windows: the engine's FrameTiming totals are reset
        # periodically by its own reports, the windows cover every frame of the run.
        print('| label | commit | mode | frames | fps | cycles/frame | frame mean/max ms | >33ms | >100ms '
              '| AI | city | units | structures | path | render (ms per frame) | house update max (h) '
              '| house updates >33ms | load |')
        print('|' + '---|' * 18)
        for r in loops:
            w = r['windows']
            frame = w.get('frame', {'count': 0, 'mean_ms': 0, 'max_ms': 0, 'over_33ms': 0, 'over_100ms': 0})
            frames = max(1, frame['count'])
            mean = lambda scope: w.get(scope, {}).get('mean_ms', 0)
            houses = {k: v for k, v in w.items() if k.startswith('house.update/')}
            worst = max(houses.items(), key=lambda kv: kv[1]['max_ms']) if houses else ('-', {'max_ms': 0})
            cycles = max((h['count'] for h in houses.values()), default=0)
            print('| %s | %s | %s | %d | %.1f | %.2f | %.1f/%.0f | %.1f%% | %.1f%% | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f (%s) | %d | %.1f |' % (
                r['label'], r['commit'], r['mode'], frame['count'], 1000.0 / frame['mean_ms'] if frame['mean_ms'] else 0,
                cycles / frames, frame['mean_ms'], frame['max_ms'], 100.0 * frame['over_33ms'] / frames,
                100.0 * frame['over_100ms'] / frames, mean('frame.ai'), mean('frame.city'), mean('frame.units'),
                mean('frame.structures'), mean('frame.path'), mean('frame.render'), worst[1]['max_ms'],
                worst[0].split('/')[-1], sum(h['over_33ms'] for h in houses.values()), r['load']))
        print()
    if not rows:
        return
    print('| label | commit | scenario | cycles | units | sim ms/cycle mean/p95/max | AI house p50/p95/max (worst house) '
          '| city mean/max | units mean | path mean | render mean | frame mean/p95/max | >33ms | >100ms | est fps | load |')
    print('|' + '---|' * 16)
    for r in rows:
        worst = max(r['house'].items(), key=lambda kv: kv[1]['p95']) if r['house'] else ('-', {'p50': 0, 'p95': 0, 'max': 0})
        worst_max = max((h['max'] for h in r['house'].values()), default=0)
        frames = max(1, r['frames'])
        print('| %s | %s | %s | %d | %d | %.2f/%.2f/%.1f | %.2f/%.2f/%.1f (h%s) | %.2f/%.1f | %.2f | %.2f | %.2f | %.1f/%.1f/%.1f | %.1f%% | %.1f%% | %.1f | %.1f |' % (
            r['label'], r['commit'], Path(r['scenario']).name, r['cycles'], r['units_end'],
            r['sim']['mean'], r['sim']['p95'], r['sim']['max'], worst[1]['p50'], worst[1]['p95'], worst_max, worst[0],
            r['city']['mean'], r['city']['max'], r['units']['mean'], r['path']['mean'], r['render']['mean'],
            r['frame']['mean'], r['frame']['p95'], r['frame']['max'], 100.0 * r['frames_over_33ms'] / frames,
            100.0 * r['frames_over_100ms'] / frames, r['est_fps'], r['load']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source', type=Path, default=root, help='Source tree whose probe runs (default: this one)')
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--prebuilt-probe', type=Path)
    parser.add_argument('--scenario', choices=sorted(SCENARIOS) + ['eight-house'], default='stefan-map')
    parser.add_argument('--save', type=Path)
    parser.add_argument('--save-profile', type=Path, help='With --save: profile whose workshop/revisions has the pinned revisions')
    parser.add_argument('--map', type=Path)
    parser.add_argument('--house', default='atreides')
    parser.add_argument('--seed', type=int, default=984458614)
    parser.add_argument('--game-options', type=Path)
    parser.add_argument('--difficulty', choices=('easy', 'medium', 'hard', 'brutal'), default='brutal')
    parser.add_argument('--mod', default='dunecity')
    parser.add_argument('--minutes', type=float, default=10.0, help='Game minutes to measure')
    parser.add_argument('--frame-cycles', type=int, default=10)
    parser.add_argument('--render-every', type=int, default=0, help='Render a frame every N cycles (0 = no rendering)')
    parser.add_argument('--mainloop', action='store_true',
                        help='Run the interactive main loop (engine frame pacing at the options\' game speed) headless')
    parser.add_argument('--rendered', action='store_true',
                        help='Run the interactive main loop in a real window with the platform renderer')
    parser.add_argument('--viewport', help='Window size for --rendered (default 1440x900, Stefan\'s)')
    parser.add_argument('--path-workers', type=int, default=0)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--label', default='run')
    parser.add_argument('--results', type=Path)
    parser.add_argument('--csv', help='Per-cycle CSV output path')
    parser.add_argument('--motion-csv', help='Unit motion trace: positions and movement state of every unit that moved, '
                        'per cycle (stepped) or per rendered frame (--mainloop/--rendered); game-perf.inc MotionTrace')
    parser.add_argument('--dump-map', help='Write the map text the match was started from (e.g. a save\'s)')
    parser.add_argument('--wall-timeout', type=int, default=3600)
    parser.add_argument('--scratch')
    parser.add_argument('--summary', type=Path, nargs='+')
    parser.add_argument('--check', action='store_true',
                        help='perf_regression: fail unless the stepped run stays inside the budgets below, each a '
                             'multiple of this process\'s calibration loop (game-perf.inc calibrationMs) so they '
                             'travel between machines and loads')
    parser.add_argument('--budget-cpu-mean', type=float, default=0.25,
                        help='mean simulation CPU per cycle / calibration (1.0.847 on the mini: 0.15)')
    parser.add_argument('--budget-house-p99', type=float, default=1.0,
                        help='worst house\'s p99 update / calibration')
    parser.add_argument('--budget-house-max', type=float, default=10.0,
                        help='worst single house update / calibration (1.0.847: ~4.6; Stefan\'s slow session: 8-26)')
    parser.add_argument('--menu-launch', type=int, default=0, metavar='SECONDS',
                        help='Also play SECONDS of a game started the way a player does (main menu -> Random '
                             'Skirmish -> Start, tests/performance/menu-launch.inc), headless unless --rendered; '
                             'with --check its per-frame time outside the measured components is budgeted. '
                             '--repeat 0 runs only this')
    parser.add_argument('--budget-menu-other', type=float, default=0.3,
                        help='menu launch: mean per-frame time outside simulation and rendering / calibration '
                             '(mini, headless: 0.032)')
    parser.add_argument('--eight-house', type=float, default=0, metavar='MINUTES',
                        help='Also continue the 8-house 384x384 save (--scenario eight-house) for MINUTES game '
                             'minutes, stepped; with --check its own budgets below apply')
    parser.add_argument('--budget-eight-cpu-p99', type=float, default=EIGHT_BUDGETS['cpu_p99'],
                        help='8-house: p99 simulation CPU per cycle / calibration')
    parser.add_argument('--budget-eight-mainloop-frame-p99', type=float, default=EIGHT_BUDGETS['mainloop_frame_p99'],
                        help='8-house main loop (headless, 2 ms, frame pacing): p99 frame wall time / calibration')
    parser.add_argument('--budget-eight-frame-p99', type=float, default=EIGHT_BUDGETS['frame_cpu_p99'],
                        help='8-house: p99 of the modelled frame (--frame-cycles cycles of simulation CPU) / calibration')
    args = parser.parse_args()
    if args.summary:
        summarize(args.summary)
        return
    require_free_space()
    with scratch_dir('game-perf', args.scratch) as scratch:
        runs = [lambda index=index: run_one(args, scratch, index) for index in range(args.repeat)]
        if args.menu_launch:
            runs.append(lambda: run_menu_launch(args, scratch))
        if args.eight_house:
            eight = argparse.Namespace(**vars(args))
            eight.scenario, eight.minutes, eight.label = 'eight-house', args.eight_house, args.label + '-eight-house'
            eight.save = eight.map = eight.save_profile = eight.csv = None
            eight.mainloop = eight.rendered = False
            runs.append(lambda: run_one(eight, scratch, args.repeat + 1))
            # The same minutes through the interactive main loop (headless, 2 ms game speed, frame
            # pacing): its cycles must digest exactly as the stepped run's, its frames stay bounded.
            loop = argparse.Namespace(**vars(eight))
            # Path search on its worker threads, as players run it: main-thread path time stays
            # below the floor that lets the frame-rate path budget adaptation act.
            loop.mainloop, loop.label, loop.path_workers = True, eight.label + '-mainloop', 0
            runs.append(lambda: run_one(loop, scratch, args.repeat + 2))
        failed = False
        stepped_digests = {}
        for run in runs:
            result = run()
            line = json.dumps(result)
            if args.results:
                with args.results.open('a') as handle:
                    handle.write(line + '\n')
            if result['mode'] == 'stepped':
                print('label=%s cycles=%d sim_mean=%.2fms cpu_mean=%.2fms frame_mean=%.1fms est_fps=%.1f worst_house_max=%.1fms'
                      % (result['label'], result['cycles'], result['sim']['mean'], result['cpu']['mean'],
                         result['frame']['mean'], result['est_fps'],
                         max((h['max'] for h in result['house'].values()), default=0)), flush=True)
            elif result['mode'] == 'menu-launch':
                print('label=%s mode=menu-launch frames=%d fps=%.1f cycles/s=%.0f frame_mean=%.2fms other/frame=%.2fms %s'
                      % (result['label'], result['frames'], result['fps'], result['cycles_per_s'], result['frame_ms'],
                         result['other_ms'], ' '.join('%s=%.2f' % kv for kv in result['per_frame_ms'].items())), flush=True)
            else:
                frame = result['windows'].get('frame', {'count': 0, 'mean_ms': 0})
                print('label=%s mode=%s frames=%d fps=%.1f frame_mean=%.1fms ai/frame=%.1fms render/frame=%.1fms'
                      % (result['label'], result['mode'], frame['count'], 1000.0 / frame['mean_ms'] if frame['mean_ms'] else 0,
                         frame['mean_ms'], result['windows'].get('frame.ai', {}).get('mean_ms', 0),
                         result['windows'].get('frame.render', {}).get('mean_ms', 0)), flush=True)
            if result['mode'] == 'stepped':
                stepped_digests[result['scenario']] = result['digests']
            if args.check:
                failures = check_budgets(result, args, stepped_digests)
                for failure in failures:
                    print('PERF_BUDGET_FAILED: %s: %s' % (result['label'], failure), flush=True)
                failed = failed or bool(failures)
        if args.check:
            if failed:
                sys.exit(1)
            print('PERF_BUDGET_OK', flush=True)


def check_budgets(result, args, stepped_digests=None):
    """perf_regression budgets, as multiples of the calibration loop measured in the same process."""
    if result['mode'] == 'menu-launch':
        cal = result['calibration_ms']
        measured = {'menu_other': (result['other_ms'] / cal, args.budget_menu_other)}
    elif result['mode'] == 'mainloop' and result['scenario'] == 'eight-house':
        # Frame pacing: the interactive loop's PROBE_DIGEST lines at the 750-cycle checkpoints are
        # the stepped run's (each list ends with its run's final digest, at different cycles), and
        # the frames stay bounded.
        reference = (stepped_digests or {}).get('eight-house', [])
        checkpoints = [d for d in result['digests'] if d in reference or int(d.split()[0][6:]) % 750 == 0]
        common = min(len(reference) - 1, len(checkpoints))
        if reference[:common] != checkpoints[:common]:
            return ['main-loop digests differ from the stepped run: %s vs %s' % (checkpoints[:common], reference[:common])]
        if common < 2:
            print('PERF_PACING_WARNING: only %d checkpoints before the path budget changed (%s)'
                  % (common, result.get('budget_changed')), flush=True)
        cal = result['calibration_ms']
        measured = {'eight_mainloop_frame_p99': (result['frame_p99'] / cal, args.budget_eight_mainloop_frame_p99)}
        print('PERF_PACING: %d checkpoint digests match; frames=%d p50/p95/p99=%.0f/%.0f/%.0f ms, %d catch-up limited, '
              '%.0f cycles/s' % (common, result['histogram_frames'], result['frame_p50'], result['frame_p95'],
                                 result['frame_p99'], result['catchup_limited_frames'], result['cycles_per_s']), flush=True)
    elif result['mode'] != 'stepped':
        return ['--check needs a stepped run (no --mainloop/--rendered)']
    elif result['scenario'] == 'eight-house':
        cal = result['calibration_ms']
        measured = {
            'eight_cpu_p99': (result['cpu']['p99'] / cal, args.budget_eight_cpu_p99),
            'eight_frame_cpu_p99': (result['frame_cpu']['p99'] / cal, args.budget_eight_frame_p99),
        }
    else:
        cal = result['calibration_ms']
        houses = result['house'].values()
        measured = {
            'cpu_mean': (result['cpu']['mean'] / cal, args.budget_cpu_mean),
            'house_p99': (max(h['p99'] for h in houses) / cal, args.budget_house_p99),
            'house_max': (max(h['max'] for h in houses) / cal, args.budget_house_max),
        }
    print('PERF_BUDGET: calibration=%.2fms ' % cal + ' '.join('%s=%.3f/%.3f' % (k, v, b) for k, (v, b) in measured.items()),
          flush=True)
    return ['%s %.3f > budget %.3f (x calibration %.2f ms)' % (k, v, b, cal) for k, (v, b) in measured.items() if v > b]


if __name__ == '__main__':
    main()
