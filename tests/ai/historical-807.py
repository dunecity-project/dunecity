#!/usr/bin/env python3
"""Whole-commit 32adfb8a cutover, unchanged engine/AI/content, exact Vanilla OFF maps.

Cluster-only. Recompiles only a diagnostic main against original native objects.
The observer reads native production snapshots, live ownership and built ledgers.
See historical-cutover.txt for source identity and estimator semantics.
"""
import argparse
from contextlib import contextmanager
import configparser
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[2]
fixtures = root / 'tests/ai/historical-807-fixtures'
RELEASE_SOURCE = '4c3ee80b784105bc665d7f1dc9c29c971df08583'
SOURCE_PHASE = 'before'
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
SUPPORTED_OPTIONS = {
    'game speed', 'concrete required', 'fog of war', 'start with explored map', 'instant build',
    'only one palace', 'rocket-turrets need power', 'sandworms respawn', 'killed sandworms drop spice',
    'manual carryall drops', 'maximum number of units override', 'maximum number of harvesters override',
    'maximum number of construction yards override', 'immortal human player', 'city effects', 'original unit damage'}
UNSUPPORTED_OPTIONS = {'research lab', 'research pace', 'slow city year', 'starport population'}
META = ('id', 'mod', 'difficulty', 'seed', 'reference_research_cells')
GAME_FIELDS = META + ('baseline_release_source', 'source_commit', 'map_sha256', 'options_sha256',
    'probe_sha256', 'native_options_hash', 'config_hash', 'content_hash', 'object_data_hash', 'ai_content_hash',
    'city_sim', 'tech_level', 'native_mix_slots', 'phase', 'cycle', 'winner_team', 'outcome', 'wall_s', 'native_capture', 'unsupported_options', 'error')
HOUSE_FIELDS = ('cycle', 'house', 'team', 'alive', 'credits', 'structures', 'army_units', 'army_value',
    'kill_value', 'loss_value', 'tech_level', 'ix', 'ix_built', 'first_ix_owned_cycle', 'first_ix_completed_cycle',
    'eliminated_cycle', 'first_sonic_built_cycle', 'sonic_producible', 'ground_cap')
COMPOSITION_FIELDS = ('cycle', 'house', 'slot', 'item', 'live_count', 'live_value', 'queued_count',
    'queued_value', 'target_bps', 'target_observed', 'allocation_cycle', 'allocated', 'learning')
UNIT_FIELDS = ('cycle', 'house', 'item', 'price', 'live_count', 'live_value', 'queued_count', 'queued_value',
    'registered_actual', 'built', 'lost', 'raw_damage', 'damage_value_milli', 'kill_bonus_milli',
    'conversion_value_milli', 'unit_killing_blows')
ALLOCATION_FIELDS = ('cycle', 'house', 'slot', 'item', 'target_bps', 'allocated', 'score', 'learning',
    'military_value', 'anti_air_floor_bps', 'infantry_quota_bps', 'sonic_producible', 'ground_cap',
    'ix', 'ix_built', 'reward_milli', 'loss_milli')
PRIOR_FIELDS = ('cycle', 'house', 'slot', 'item', 'fieldable', 'start_bps', 'tuned_weight',
    'tuned_bounded_bps', 'strategy_prior_bps', 'mix', 'tempo', 'band')
BUILD_FIELDS = ('cycle', 'house', 'item', 'built', 'delta', 'first_ix_completed_cycle')


def read(path):
    path = Path(path)
    if not path.is_file():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def append(path, fields, rows):
    path = Path(path)
    fresh = not path.exists()
    with path.open('a', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        if fresh:
            writer.writeheader()
        writer.writerows(rows)


def fixture_games():
    """Bind each immutable game to its original per-mod research-OFF bytes."""
    manifest = json.loads((fixtures / 'manifest.json').read_text())
    return [dict(game, game_options=manifest['off_options_by_mod'][game['mod']])
            for game in manifest['games']]


def schedule(args):
    """24 Vanilla OFF contexts; each source run once, not doubled by reference mappings."""
    games = fixture_games()
    return [game for game in games if (not args.mod or game['mod'] == args.mod)
            and game['seed'] in args.seeds]


def windows_argv(command):
    # Same ABI-compatible clang-cl recipe as the current balance probe.
    import ctypes
    from ctypes import wintypes
    split = ctypes.WinDLL('shell32').CommandLineToArgvW
    split.restype = ctypes.POINTER(wintypes.LPWSTR)
    split.argtypes = (wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int))
    count = ctypes.c_int()
    argv = split(command, ctypes.byref(count))
    try:
        return [argv[i] for i in range(count.value)]
    finally:
        ctypes.WinDLL('kernel32').LocalFree(argv)


def windows_probe_commands(build):
    database = json.loads(subprocess.check_output(['ninja', '-C', str(build), '-t', 'compdb', '-x'], text=True))
    def output(entry):
        return entry.get('output', '').replace('\\', '/')
    main_object = '/dunecity.dir/main.cpp.obj'
    compile_entry = next(e for e in database if ('/' + output(e)).endswith(main_object))
    link_entry = next(e for e in database if output(e).endswith('/dunecity.exe') and 'vs_link_exe' in e['command'])
    compile_command = windows_argv(compile_entry['command'])
    if Path(compile_command[0]).name.lower().startswith(('sccache', 'ccache')):
        compile_command = compile_command[1:]
    clang = os.environ.get('DUNECITY_CLANG_CL') or shutil.which('clang-cl')
    if not clang:
        clang = str(Path(compile_command[0]).parents[5] / 'Llvm/x64/bin/clang-cl.exe')
    if not Path(clang).is_file():
        raise RuntimeError('clang-cl is required for the private historical main')
    command = [clang]
    for arg in compile_command[1:]:
        if arg == '/showIncludes' or arg.startswith('/Fd'):
            continue
        command.append('/Fo@OBJECT@' if arg.startswith('/Fo') else arg)
    command[command.index(next(a for a in command if a in ('-c', '/c'))) + 1] = '@SOURCE@'
    command += ['-Xclang', '-fno-access-control']
    text = link_entry['command']
    if '/C "' in text:
        text = text[text.index('/C "') + 4:text.rindex('"')]
    segment = next(s for s in text.split(' && ') if 'vs_link_exe' in s or 'link.exe' in s.lower())
    link = []
    for arg in windows_argv(segment):
        lower = arg.lower()
        if arg.startswith('--intdir='):
            arg = '--intdir=@STAGING@'
        elif lower.startswith('/out:'):
            arg = '/out:@BINARY@'
        elif lower.startswith('/implib:'):
            arg = '/implib:@STAGING@\\historical-807-probe.lib'
        elif lower.startswith('/pdb:'):
            arg = '/pdb:@STAGING@\\historical-807-probe.pdb'
        elif arg.replace('\\', '/').endswith(main_object):
            arg = '@OBJECT@'
        link.append(arg)
    if '@OBJECT@' not in link or '/out:@BINARY@' not in link:
        raise RuntimeError('Unexpected Windows link recipe')
    return command, link, (build / output(link_entry)).parent


def build_probe(args, scratch):
    """Build only a private main, never configure another native build tree."""
    build = Path(args.build_dir).resolve()
    scratch = Path(scratch).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Historical main-menu injection point changed')
    main = main.replace(needle, 'int menuResult = runHistorical807Probe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                        'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    observer = (root / 'tests/ai/historical-807-observer.inc').as_posix()
    main = main[:position] + '#include "' + observer + '"\n' + main[position:]
    source = scratch / 'historical-main.cpp'
    source.write_text(main)
    windows = sys.platform == 'win32'
    if windows:
        compile_command, link, game_dir = windows_probe_commands(build)
        obj = scratch / 'historical-main.obj'
        token = hashlib.sha256(str(scratch).encode()).hexdigest()[:12]
        binary = game_dir / ('historical-807-probe-' + token + '.exe')
    else:
        target = 'bin/dunecity.app/Contents/MacOS/dunecity'
        commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
        compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
        for option in ('-include', '-MT', '-MF'):
            if option in compile_command:
                position = compile_command.index(option)
                del compile_command[position:position + 2]
        for option in ('-MD', '-MMD'):
            if option in compile_command:
                compile_command.remove(option)
        compile_command[compile_command.index('-o') + 1] = '@OBJECT@'
        compile_command[compile_command.index('-c') + 1] = '@SOURCE@'
        compile_command.append('-fno-access-control')
        link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
        link = link[link.index('&&') + 1:]
        link = link[:link.index('&&')]
        link[link.index('-o') + 1] = '@BINARY@'
        link = ['@OBJECT@' if arg.endswith('/main.cpp.o') else arg for arg in link]
        obj = scratch / 'historical-main.o'
        contents = scratch / 'historical-807-probe.app/Contents'
        (contents / 'MacOS').mkdir(parents=True, exist_ok=True)
        resources = contents / 'Resources'
        if not resources.exists():
            resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
        binary = contents / 'MacOS/historical-807-probe'
    replacements = {'@OBJECT@': str(obj), '@SOURCE@': str(source), '@BINARY@': str(binary), '@STAGING@': str(scratch)}
    def fill(arg):
        for old, value in replacements.items():
            arg = arg.replace(old, value)
        return arg
    with (scratch / 'build.log').open('w') as log:
        subprocess.run([fill(arg) for arg in compile_command], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([fill(arg) for arg in link], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


@contextmanager
def scratch_dir(purpose, explicit=None):
    # Ordinary units have node-owned explicit scratch. Preparation parity does
    # not set CLUSTER_SCRATCH, so use the installed lease helper there, as the
    # current probe_profile.scratch_dir does. Never create another build tree.
    if explicit:
        path = Path(explicit).resolve()
        path.mkdir(parents=True, exist_ok=True)
        yield path
        return
    helper = Path.home() / '.local/share/agent-workspace/scratch.py'
    config = Path.home() / '.config/agent-workspace/projects.json'
    managed = None
    if helper.is_file() and config.is_file():
        spec = importlib.util.spec_from_file_location('historical_807_scratch', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        candidate = module.Scratch()
        if 'dunecity' in candidate.projects:
            managed = candidate
    if managed:
        path = managed.create('dunecity', os.environ.get('AGENT_WORKSPACE_OWNER', 'cluster:%d' % os.getpid()), purpose)
    else:
        path = Path(tempfile.mkdtemp(prefix='dunecity-' + purpose + '-'))
    failed = True
    try:
        yield path
        failed = (path / '.historical-failed').exists()
    finally:
        if managed:
            managed.finish(path, not failed)
        elif not failed:
            shutil.rmtree(path)


def finish_game(scratch, run_dir, failed):
    if failed:
        (Path(scratch) / '.historical-failed').touch()
    else:
        shutil.rmtree(run_dir)


def option_receipt(path):
    contents = Path(path).read_bytes()
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(contents.decode('utf-8'))
    options = dict(config.items('Game Options'))
    unknown = options.keys() - SUPPORTED_OPTIONS - UNSUPPORTED_OPTIONS
    if unknown:
        raise ValueError('Unreviewed historical options: ' + ','.join(sorted(unknown)))
    if config.getboolean('Game Options', 'Research Lab', fallback=False):
        raise ValueError('Historical source has no Research Lab; cannot silently run an ON command OFF')
    return {'sha256': hashlib.sha256(contents).hexdigest(),
            'applied_supported_keys': sorted(options.keys() & SUPPORTED_OPTIONS),
            'unsupported_requested_values': {key: options[key] for key in sorted(options.keys() & UNSUPPORTED_OPTIONS)}}


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def play(args, scratch, binary, game):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    out = Path(scratch) / ('h807-' + hashlib.sha256(game['id'].encode()).hexdigest()[:12])
    out.mkdir(parents=True, exist_ok=False)
    options_path = Path(args.game_options) if args.game_options else fixtures / game['game_options']['file']
    options = option_receipt(options_path)
    if options['sha256'] != game['game_options']['sha256']:
        raise ValueError('Game options differ from original per-mod OFF input: ' + game['mod'])
    map_file = fixtures / 'maps' / game['map']
    map_sha = hashlib.sha256(map_file.read_bytes()).hexdigest()
    if map_sha != game['map_sha256']:
        raise ValueError('Frozen map content differs from archived input: ' + game['id'])
    profile = out / 'profile'
    profile.mkdir()
    (profile / 'Dune City.ini').write_text(
        '[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\n'
        'Fullscreen = false\n[General]\nPlay Intro = false\nDiagnostic Logs = true\n')
    numeric_sides = ','.join(str(HOUSE_NUMBERS[name]) + ':' + team + ':' + controller
        for name, team, controller in (side.split(':') for side in game['sides'].split(',')))
    source_commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    setup = {'game': game, 'options': options, 'original_source': RELEASE_SOURCE, 'source_commit': source_commit,
             'phase': SOURCE_PHASE, 'map_sha256': map_sha, 'observer': 'native local spectator, no registered human',
             'research': 'unsupported/off', 'initial_tech': 9, 'path_workers': 1}
    (out / 'setup.json').write_text(json.dumps(setup, indent=2) + '\n')
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
        DUNECITY_PATH_WORKERS='1', H807_MOD=game['mod'], H807_MAP=str(map_file), H807_MAP_NAME=game['map_name'],
        H807_SEED=str(game['seed']), H807_MINUTES=str(game.get('minutes', 40)), H807_SIDES=numeric_sides,
        H807_GAME_OPTIONS=str(options_path.resolve()), H807_CAPTURE_MIB=str(args.capture_mib))
    started = time.monotonic()
    error = ''
    try:
        with (out / 'run.log').open('w') as log:
            process = subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=args.wall_timeout)
        if process.returncode:
            error = 'rc=' + str(process.returncode)
    except subprocess.TimeoutExpired:
        error = 'wall_timeout'
    lines = (out / 'run.log').read_text(errors='replace').splitlines()
    native_setups = [parse(line, 'HISTORICAL_SETUP:') for line in lines if 'HISTORICAL_SETUP:' in line]
    if len(native_setups) != 1:
        error = error or 'missing_or_duplicate_native_setup'
    native_setup = native_setups[0] if native_setups else {}
    endings = [parse(line, 'HISTORICAL_RESULT:') for line in lines if 'HISTORICAL_RESULT:' in line]
    if len(endings) != 1:
        error = error or 'missing_or_duplicate_native_result'
    ending = endings[0] if endings else {}
    events = []
    for event_file in sorted(profile.rglob('events.jsonl')):
        with event_file.open() as handle:
            events.extend(json.loads(line) for line in handle if line.strip())
    summaries = [event for event in events if event.get('event') == 'game_summary']
    sessions = [event for event in events if event.get('event') == 'session_start']
    if len(summaries) != 1 or len(sessions) != 1:
        error = error or 'incomplete_native_summary'
    # Retain original-house native ledgers without mistaking them for live ownership.
    (out / 'summary.json').write_text(json.dumps({'setup': setup, 'native_setup': native_setup, 'result': ending,
        'session_start': sessions, 'game_summary': summaries}, indent=2) + '\n')
    metadata = {key: game[key] for key in ('id', 'mod', 'difficulty', 'seed')}
    metadata['reference_research_cells'] = ','.join(sorted(game.get('reference_research_cells', ['off'])))
    row = dict(metadata, baseline_release_source=RELEASE_SOURCE, source_commit=source_commit,
        map_sha256=map_sha, options_sha256=options['sha256'], probe_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
        native_options_hash=native_setup.get('options_hash', ''), config_hash=native_setup.get('config_hash', ''),
        content_hash=native_setup.get('content_hash', ''), object_data_hash=native_setup.get('object_data_hash', ''),
        ai_content_hash=native_setup.get('ai_content_hash', ''), city_sim=native_setup.get('city_sim', ''),
        tech_level=native_setup.get('tech_level', ''), native_mix_slots=native_setup.get('native_mix_slots', ''), phase=SOURCE_PHASE,
        cycle=ending.get('cycle', ''), winner_team=ending.get('winner_team', ''), outcome=ending.get('outcome', ''),
        wall_s=round(time.monotonic() - started, 3), native_capture='summary_present_not_stream_certified' if summaries else 'missing',
        unsupported_options=json.dumps(options['unsupported_requested_values'], sort_keys=True), error=error)
    if error:
        (results / ('failed-' + out.name + '.txt')).write_text(error + '\n' + '\n'.join(lines[-80:]) + '\n')
    else:
        for filename, marker, fields in (
                ('composition.csv', 'HISTORICAL_COMPOSITION:', COMPOSITION_FIELDS),
                ('units.csv', 'HISTORICAL_UNIT:', UNIT_FIELDS),
                ('samples.csv', 'HISTORICAL_HOUSE:', HOUSE_FIELDS),
                ('allocations.csv', 'HISTORICAL_ALLOCATION:', ALLOCATION_FIELDS),
                ('priors.csv', 'HISTORICAL_PRIOR:', PRIOR_FIELDS),
                ('builds.csv', 'HISTORICAL_BUILD:', BUILD_FIELDS)):
            rows = [dict(metadata, **parse(line, marker)) for line in lines if marker in line]
            append(results / filename, META + fields, rows)
            if filename == 'samples.csv':
                final = {sample['house']: sample for sample in rows}
                append(results / 'houses.csv', META + HOUSE_FIELDS, list(final.values()))
        append(results / 'games.csv', GAME_FIELDS, [row])  # Completion marker is always last.
    finish_game(scratch, out, bool(error))
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    good = [row for row in rows if not row.get('error')]
    print('Historical cutover %s: %d collected games; original engine/AI/content %s' % (SOURCE_PHASE, len(good), RELEASE_SOURCE))
    print('Exact archived Vanilla OFF maps/rosters/options, T9; unsupported options are not emulated.')
    print('Targets: native production-pass snapshots; 8 grouped slots BEFORE, 10 per-special slots AFTER.')
    print('Live: present owner, positive health, replacement price. Built/lost/rewards: original native ledgers, not payments.')
    return any(row.get('error') for row in rows)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', required=True)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--mod', choices=('vanilla',), default='vanilla')
parser.add_argument('--seeds', type=int, nargs='+', default=[9101, 148680876, 55001, 77001])
parser.add_argument('--game-options', type=Path,
                    help='Frozen override; bytes must match the manifest OFF input for this mod')
parser.add_argument('--capture-mib', type=int, choices=range(256, 4097), default=1024)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--list', action='store_true')
args = parser.parse_args() if __name__ == '__main__' else None
if args is not None:
    if args.list:
        for game in schedule(args):
            print(game['id'], game['sides'], game['map_sha256'])
    else:
        raise SystemExit('Submit through dunecity-queue; direct local games are intentionally disabled')
