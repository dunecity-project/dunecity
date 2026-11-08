#!/usr/bin/env python3
"""Exact-map, whole-release native AI league; cluster only, Python 3.9.

Only a private diagnostic main is recompiled. Unchanged engine/AI/content native
objects execute all games. Fixture IDs, roster, maps and requested options are
immutable. Missing historical research options are disclosed, never emulated.
See docs/ai-baseline-gate.md for the independent-objective gate and provenance.
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
fixtures = root / 'tests/ai/baseline-fixtures'
HOUSE_NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
META = ('id', 'mod', 'difficulty', 'seed', 'reference_research_cells')
HOUSE_FIELDS = ('cycle', 'minute', 'house', 'team', 'alive', 'credits', 'structures', 'army_units', 'army_value',
    'kill_value', 'loss_value', 'tech_level', 'first_inactive_cycle', 'terminal_inactive_since_cycle',
    'revival_count', 'inactive_cycles', 'harvesters', 'refineries', 'population',
    'zone_residential', 'zone_industrial', 'zone_commercial', 'zone_residential_built', 'zone_industrial_built',
    'zone_commercial_built', 'power', 'need', 'built', 'structures_lost', 'base_hp', 'yards', 'yard_level',
    'queue', 'upgrading', 'waiting', 'research_enabled', 'research_target', 'research_remaining') + tuple(
    field for name in ('light','heavy','ix','starport','palace') for field in
    (name, name+'_built', 'first_'+name+'_owned_cycle', 'first_'+name+'_completed_cycle'))
UNIT_FIELDS = ('cycle', 'house', 'item', 'price', 'live_count', 'live_value', 'queued_count', 'queued_value',
    'registered_actual', 'built', 'lost')
WAVE_FIELDS = ('house', 'cycle', 'members', 'value')
GAME_FIELDS = META + ('release', 'baseline_release_source', 'source_commit', 'map_sha256', 'options_sha256',
    'probe_sha256', 'native_options_hash', 'config_hash', 'content_hash', 'object_data_hash', 'ai_content_hash',
    'city_sim', 'tech_level', 'cycle', 'winner_team', 'outcome', 'wall_s', 'host', 'native_capture',
    'telemetry_status', 'telemetry_reasons', 'telemetry_events', 'research', 'unsupported_options',
    'style', 'style_sha256', 'genome_phases_sha256', 'actor_config_hashes', 'error')


def fixture_games():
    manifest = json.loads((fixtures / 'manifest.json').read_text())
    games = []
    for native in manifest['games']:
        parts = native['id'].split('/')
        matrix = parts.index('matrix')
        options = manifest[
            'on_options_by_mod' if native.get('research', 'off') == 'on' else 'off_options_by_mod'][native['mod']]
        games.append(dict(native, game_options=options, format=parts[matrix+4], spice_level=parts[matrix+3]))
    return games


def schedule(args):
    return [game for game in fixture_games() if (not args.mod or game['mod'] == args.mod)
            and game['seed'] in args.seeds and game['difficulty'] in args.difficulties
            and game.get('research', 'off') == args.research
            and game['format'] in getattr(args, 'formats', ('1v1','ffa6'))
            and game['spice_level'] in getattr(args, 'spice', ('none','medium'))]

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
            arg = '/implib:@STAGING@\\ai-baseline-probe.lib'
        elif lower.startswith('/pdb:'):
            arg = '/pdb:@STAGING@\\ai-baseline-probe.pdb'
        elif arg.replace('\\', '/').endswith(main_object):
            arg = '@OBJECT@'
        link.append(arg)
    if '@OBJECT@' not in link or '/out:@BINARY@' not in link:
        raise RuntimeError('Unexpected Windows link recipe')
    return command, link, (build / output(link_entry)).parent

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
        spec = importlib.util.spec_from_file_location('ai_baseline_scratch', helper)
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
        failed = (path / '.baseline-failed').exists()
    finally:
        if managed:
            managed.finish(path, not failed)
        elif not failed:
            shutil.rmtree(path)

def finish_game(scratch, run_dir, failed):
    if failed:
        (Path(scratch) / '.baseline-failed').touch()
    else:
        shutil.rmtree(run_dir)


def probe_source():
    """Generate the exact private TU, also usable for committed-tree syntax checks."""
    main = (root / 'src/main.cpp').read_text(encoding='utf-8')
    # The private Windows probe uses clang-cl for access-control diagnostics. Unlike
    # native MSVC, it defines __clang__ but has no Itanium ABI demangler header.
    abi_guard = '#if defined( __clang__ ) || defined(__GNUG__) || defined( __GLIBCXX__ ) || defined( __GLIBCPP__ )'
    main = main.replace(abi_guard, '#if !defined(_WIN32) && (' + abi_guard[4:] + ')')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Native main-menu injection point changed')
    main = main.replace(needle, 'int menuResult = runAIBaselineProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    headers = {name: (root / 'include' / name).read_text()
               for name in ('House.h', 'GameInitSettings.h', 'data.h', 'players/QuantBot.h', 'players/QuantBotConfig.h')}
    flags = {'RESEARCH': 'getTechResearchTarget' in headers['House.h'],
             'LOCAL_OBSERVER': 'setLocalSpectator' in headers['GameInitSettings.h'],
             'FIVE_SQUADS': 'Unit_Infantry5' in headers['data.h'],
             'CONFIG_STYLE': 'configOverride' in headers['players/QuantBot.h']
                             and 'botConfig()' in headers['players/QuantBot.h']
                             and 'applyOverrides' in headers['players/QuantBotConfig.h'],
             'GENOME_PHASE': (root / 'tests/ai/genome-phases.inc').is_file(),
             'NATIVES': 'isNatives()' in headers['House.h']}
    definitions = ''.join('#define BASELINE_HAS_%s %d\n' % (name, enabled) for name, enabled in flags.items())
    position = main.index('int main(')
    observer = (root / 'tests/ai/ai-baseline-observer.inc').as_posix()
    return main[:position] + definitions + '#include "' + observer + '"\n' + main[position:]


def build_probe(args, scratch):
    """Reuse the node's normal native objects and compiler/link recipe, no new build tree."""
    build, scratch = Path(args.build_dir).resolve(), Path(scratch).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    source = scratch / 'baseline-main.cpp'
    source.write_text(probe_source(), encoding='utf-8')
    if sys.platform == 'win32':
        compile_command, link, game_dir = windows_probe_commands(build)
        obj = scratch / 'baseline-main.obj'
        binary = game_dir / ('ai-baseline-probe-' + hashlib.sha256(str(scratch).encode()).hexdigest()[:12] + '.exe')
    else:
        target = 'bin/dunecity.app/Contents/MacOS/dunecity'
        commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
        compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
        for option in ('-include', '-MT', '-MF'):
            if option in compile_command:
                index = compile_command.index(option)
                del compile_command[index:index + 2]
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
        obj = scratch / 'baseline-main.o'
        contents = scratch / 'ai-baseline-probe.app/Contents'
        (contents / 'MacOS').mkdir(parents=True, exist_ok=True)
        (contents / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
        binary = contents / 'MacOS/ai-baseline-probe'
    replacements = {'@OBJECT@': str(obj), '@SOURCE@': str(source), '@BINARY@': str(binary), '@STAGING@': str(scratch)}
    def fill(arg):
        for old, value in replacements.items():
            arg = arg.replace(old, value)
        return arg
    with (scratch / 'build.log').open('w') as log:
        subprocess.run([fill(arg) for arg in compile_command], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([fill(arg) for arg in link], cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def option_receipt(path):
    contents = Path(path).read_bytes()
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(contents.decode('utf-8'))
    research = config.getboolean('Game Options', 'Research Lab', fallback=False)
    if research and 'getTechResearchTarget' not in (root / 'include/House.h').read_text():
        raise ValueError('This native historical release has no Research Lab; cannot run ON as OFF')
    supported = {'game speed','concrete required','fog of war','start with explored map','instant build',
        'only one palace','rocket-turrets need power','sandworms respawn','killed sandworms drop spice',
        'manual carryall drops','maximum number of units override','maximum number of harvesters override',
        'maximum number of construction yards override','immortal human player','city effects','original unit damage'}
    optional = {'research lab':'researchLab','research pace':'researchPace',
                'slow city year':'slowCityYear','starport population':'starportPopulation'}
    options = dict(config.items('Game Options'))
    unknown = options.keys() - supported - optional.keys()
    if unknown:
        raise ValueError('Unreviewed baseline requested options: ' + ','.join(sorted(unknown)))
    header = (root / 'include/DataTypes.h').read_text()
    unsupported = {key:options[key] for key, token in optional.items() if key in options and token not in header}
    return dict(sha256=hashlib.sha256(contents).hexdigest(), unsupported_requested_values=unsupported,
                research='on' if research else 'off')


def style_receipt(args):
    style = getattr(args, 'style', None)
    if not style:
        return '', ''
    if Path(style).name != style or style.endswith('.ini'):
        raise ValueError('Use a committed play-style basename without its .ini suffix')
    path = root / 'tests/ai/play-styles' / (style + '.ini')
    return str(path), hashlib.sha256(path.read_bytes()).hexdigest()


def genome_receipt(args, out):
    """Snapshot adapter-materialized phase INIs; the queue does not freeze external dirs."""
    directory = getattr(args, 'genome_phase_dir', None)
    if not directory:
        return '', ''
    if getattr(args, 'style', None):
        raise ValueError('A native baseline arm may use a style OR genome phases, not both')
    directory = Path(directory).resolve()
    files = sorted(directory.glob('*.ini'))
    if not (directory / 'schedule.ini').is_file():
        raise ValueError('A materialized genome phase directory needs schedule.ini')
    snapshot = Path(out) / 'genome-phases'
    snapshot.mkdir()
    digest = hashlib.sha256()
    for path in files:
        data = path.read_bytes()
        (snapshot / path.name).write_bytes(data)
        digest.update(path.name.encode('utf-8') + b'\0' + data + b'\0')
    return str(snapshot), digest.hexdigest()


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def capture(profile, final_cycle, roster):
    """Stream once; exact dispatch counts require unbroken native session boundaries."""
    paths = sorted(profile.rglob('events.jsonl'))
    reasons, waves, summaries, starts = [], [], [], []
    count = 0
    if len(paths) != 1:
        reasons.append('missing_or_multiple_capture')
    for path in paths:
        sequence, session, ends, end_cycle = 0, None, 0, -1
        with path.open(encoding='utf-8') as source:
            for line in source:
                try:
                    event = json.loads(line)
                    seq, cycle, data = int(event['seq']), int(event['cycle']), event['data']
                    if not isinstance(data, dict) or event.get('schema_version') != 1:
                        raise ValueError('schema')
                    if seq != sequence + 1:
                        reasons.append('sequence_gap')
                    sequence = seq
                    session = session or event['session']
                    if not session or session != event['session']:
                        reasons.append('session_mismatch')
                    count += 1
                    name = event['event']
                    if name == 'session_start': starts.append(event)
                    elif name == 'session_end': ends += 1; end_cycle = cycle
                    elif name == 'game_summary': summaries.append(event)
                    elif name in ('capture_limit','simulation_exception'): reasons.append(name)
                    elif name == 'ground_hunt' and int(data.get('members',0)) > 0:
                        waves.append(dict(house=event['house'], cycle=cycle, members=data['members'], value=data['value']))
                except (ValueError, KeyError, TypeError):
                    reasons.append('malformed_record')
        if ends != 1 or end_cycle < final_cycle:
            reasons.append('missing_or_short_session_end')
    if len(starts) != 1 or len(summaries) != 1:
        reasons.append('missing_or_duplicate_native_summary')
    elif set(str(h) for h, _ in roster) != set(summaries[0]['data'].get('houses', {})):
        reasons.append('incomplete_native_roster')
    return dict(status='complete' if not reasons else 'incomplete', reasons=','.join(sorted(set(reasons))),
                events=count, waves=waves, summaries=summaries, starts=starts)


def play(args, scratch, binary, game):
    import platform
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    out = Path(scratch) / ('baseline-' + hashlib.sha256(game['id'].encode()).hexdigest()[:12])
    out.mkdir(parents=True, exist_ok=False)
    failed = True
    started = time.monotonic()
    try:
        options_path = Path(args.game_options) if args.game_options else fixtures / game['game_options']['file']
        options = option_receipt(options_path)
        style_path, style_sha = style_receipt(args)
        phases_path, phases_sha = genome_receipt(args, out)
        if options['sha256'] != game['game_options']['sha256']:
            raise ValueError('Requested options differ from exact fixture for ' + game['mod'])
        if options['research'] != game.get('research', 'off'):
            raise ValueError('Native fixture research treatment differs from requested option bytes')
        map_path = fixtures / 'maps' / game['map']
        map_sha = hashlib.sha256(map_path.read_bytes()).hexdigest()
        if map_sha != game['map_sha256']:
            raise ValueError('Exact fixture map bytes changed')
        profile = out / 'profile'
        profile.mkdir()
        (profile / 'Dune City.ini').write_text('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\nFullscreen = false\n[General]\nPlay Intro = false\nDiagnostic Logs = true\n')
        source_commit = subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'], text=True).strip()
        native_path = root / 'tests/ai/baseline-native.json'
        native = json.loads(native_path.read_text()) if native_path.is_file() else {'source_commit':source_commit, 'release':'current'}
        numeric_sides = ','.join(str(HOUSE_NUMBERS[name])+':'+team+':'+controller
            for name,team,controller in (side.split(':') for side in game['sides'].split(',')))
        setup = dict(game=game, options=options, native=native, source_commit=source_commit,
                     map_sha256=map_sha, genome_phases_sha256=phases_sha,
                     observer='native detached view, no registered human controller')
        (out / 'setup.json').write_text(json.dumps(setup,sort_keys=True,indent=2)+'\n')
        env = dict(os.environ,DUNECITY_USERDIR=str(profile),DUNECITY_AI_TELEMETRY='1',SDL_VIDEODRIVER='dummy',
            SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',DUNECITY_PATH_WORKERS='1',BASELINE_MOD=game['mod'],
            BASELINE_MAP=str(map_path),BASELINE_MAP_NAME=game['map_name'],BASELINE_SEED=str(game['seed']),
            BASELINE_MINUTES=str(game['minutes']),BASELINE_SIDES=numeric_sides,BASELINE_TECH=str(game['initial_tech']),
            BASELINE_OPTIONS=str(options_path.resolve()),BASELINE_CAPTURE_MIB=str(args.capture_mib),
            BASELINE_STYLE_FILE=style_path)
        env.pop('BALANCE_GENOME_PHASE_DIR', None)
        if phases_path:
            env['BALANCE_GENOME_PHASE_DIR'] = phases_path
        error = ''
        try:
            with (out/'run.log').open('w') as log:
                process = subprocess.run([str(binary),'--window','--showlog'],cwd=out,env=env,stdout=log,
                                         stderr=subprocess.STDOUT,timeout=args.wall_timeout)
            if process.returncode:
                error = 'native_rc='+str(process.returncode)
        except subprocess.TimeoutExpired:
            error = 'native_wall_timeout'
        parsed = {name:[] for name in ('SETUP','RESULT','HOUSE','TECH','UNIT','ROSTER','LIFECYCLE')}
        lines = (out/'run.log').read_text(errors='replace').splitlines()
        for line in lines:
            for name in parsed:
                marker = 'BASELINE_'+name+':'
                if marker in line:
                    parsed[name].append(parse(line,marker))
                    break
        if len(parsed['SETUP']) != 1 or len(parsed['RESULT']) != 1:
            raise ValueError('Missing or duplicate native setup/result; '+error)
        setup_native, ending = parsed['SETUP'][0], parsed['RESULT'][0]
        if int(setup_native['tech_level']) != game['initial_tech'] or int(setup_native['city_sim']) != int(game['mod']=='dunecity'):
            raise ValueError('Native supported game settings do not match fixture')
        if sorted((int(r['house']),int(r['team'])) for r in parsed['ROSTER']) != sorted(map(tuple,game['roster'])):
            raise ValueError('Native roster differs from exact fixture')
        cap = capture(profile,int(ending['cycle']),game['roster'])
        if cap['status'] != 'complete':
            error = error or 'incomplete_capture:'+cap['reasons']
        (out/'summary.json').write_text(json.dumps(dict(setup=setup,native_setup=setup_native,result=ending,
            session_start=cap['starts'],game_summary=cap['summaries'],telemetry_status=cap['status'],
            telemetry_reasons=cap['reasons']),sort_keys=True,indent=2)+'\n')
        meta = {key:game[key] for key in ('id','mod','difficulty','seed')}
        meta['reference_research_cells'] = game.get('research', 'off')
        row = dict(meta,release=native['release'],baseline_release_source=native['source_commit'],source_commit=source_commit,
            map_sha256=map_sha,options_sha256=options['sha256'],probe_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
            native_options_hash=setup_native['options_hash'],config_hash=setup_native['config_hash'],content_hash=setup_native['content_hash'],
            object_data_hash=setup_native['object_data_hash'],ai_content_hash=setup_native['ai_content_hash'],city_sim=setup_native['city_sim'],
            tech_level=setup_native['tech_level'],cycle=ending['cycle'],winner_team=ending['winner_team'],outcome=ending['outcome'],
            wall_s=round(time.monotonic()-started,3),host=platform.node(),native_capture='stream_certified' if not error else 'incomplete',
            telemetry_status=cap['status'],telemetry_reasons=cap['reasons'],telemetry_events=cap['events'],research=game.get('research', 'off'),
            unsupported_options=json.dumps(options['unsupported_requested_values'],sort_keys=True),
            style=getattr(args,'style',None) or ('genome' if phases_path else 'default'),style_sha256=style_sha,
            genome_phases_sha256=phases_sha,
            actor_config_hashes=','.join(item['house']+':'+item['config_hash'] for item in parsed['ROSTER']),error=error)
        if error:
            (results/('failed-'+out.name+'.txt')).write_text(error+'\n'+'\n'.join(lines[-80:])+'\n')
            return row
        tech = {(r['cycle'],r['house'],r['name']):r for r in parsed['TECH']}
        lifecycle = {(r['cycle'],r['house']):r for r in parsed['LIFECYCLE']}
        samples = []
        for sample in parsed['HOUSE']:
            state = lifecycle[(sample['cycle'],sample['house'])]
            sample.update({field:state[field] for field in ('terminal_inactive_since_cycle','revival_count','inactive_cycles')})
            if int(sample['research_enabled']) != int(game.get('research', 'off') == 'on'):
                raise ValueError('Native per-house research option differs from the exact fixture')
            for name in ('light','heavy','ix','starport','palace'):
                measured = tech[(sample['cycle'],sample['house'],name)]
                sample.update({name:measured['live'],name+'_built':measured['built'],
                    'first_'+name+'_owned_cycle':measured['first_owned_cycle'],
                    'first_'+name+'_completed_cycle':measured['first_completed_cycle']})
            samples.append(dict(meta,**sample))
        final = {r['house']:r for r in samples}
        if set(map(str,(h for h,_ in game['roster']))) != set(final) or any(int(r['cycle']) != int(ending['cycle']) for r in final.values()):
            raise ValueError('Missing terminal native house observations')
        append(results/'samples.csv',META+HOUSE_FIELDS,samples)
        append(results/'houses.csv',META+HOUSE_FIELDS,list(final.values()))
        append(results/'units.csv',META+UNIT_FIELDS,[dict(meta,**r) for r in parsed['UNIT']])
        append(results/'waves.csv',META+WAVE_FIELDS,[dict(meta,**r) for r in cap['waves']])
        append(results/'games.csv',GAME_FIELDS,[row])
        failed = False
        return row
    finally:
        finish_game(scratch,out,failed)


def report(args):
    rows = read(Path(args.results)/'games.csv')
    expected = {g['id'] for g in schedule(args)}
    collected = {r['id'] for r in rows if not r.get('error')}
    print('Historical native league: %d/%d completed exact contexts; missing=%d' % (len(collected),len(expected),len(expected-collected)))
    print('Native engine/AI/content retained. Fixed tech9 researchOFF; unsupported requested options explicitly disclosed.')
    return bool(expected != collected or any(r.get('error') for r in rows))


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results',required=True)
parser.add_argument('--build-dir',type=Path,default=root/'build-int')
parser.add_argument('--mod',choices=('dunecity','Dune2R','vanilla'))
parser.add_argument('--research',choices=('off','on'),default='off')
parser.add_argument('--formats',nargs='+',choices=('1v1','ffa6'),default=['1v1','ffa6'])
parser.add_argument('--spice',nargs='+',choices=('none','medium'),default=['none','medium'])
parser.add_argument('--style',help='Committed hashed config gene overlay; no unhashed Play Style behaviours')
parser.add_argument('--genome-phase-dir',type=Path,help='Internal: queued adapter materializes committed phase INIs per unit')
parser.add_argument('--seeds',type=int,nargs='+',default=[9101,148680876,55001,77001,1841712204,8101,8202,8303])
parser.add_argument('--difficulties',choices=('medium','hard','brutal'),nargs='+',default=['medium','hard','brutal'])
parser.add_argument('--game-options',type=Path)
parser.add_argument('--capture-mib',type=int,default=1024)
parser.add_argument('--wall-timeout',type=int,default=1800)
parser.add_argument('--list',action='store_true')
args = parser.parse_args() if __name__ == '__main__' else None
if args is not None:
    if args.list:
        for game in schedule(args):
            print(game['id'],game['sides'],game['map_sha256'])
    else:
        raise SystemExit('Submit through dunecity-queue; local league games are intentionally disabled')
