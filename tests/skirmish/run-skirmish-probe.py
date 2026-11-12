#!/usr/bin/env python3
"""Random Skirmish against the real engine, in isolated profiles.

Links a diagnostic copy of the game whose main menu is replaced by tests/skirmish/skirmish-probe.inc
(production objects are unchanged) and runs one process per case:

CTest prepares one private fixture with --prepare-only --fixture-dir DIR, then
runs each --cases CASE --size WxH [--research-payment off|on] separately against
that directory. Runtime invocations never compile an explicitly supplied fixture.
Source/build fingerprints and a verified executable seal prevent stale reuse;
a publication lock and atomic rename keep incomplete builds invisible.
Without --fixture-dir, the direct CLI still prepares and runs the requested matrix.
Each runtime uses fresh leased profiles; failed scratch and all output logs remain.

  menu         the Random Skirmish screen: house cap, persistence, Save map, screenshots
  mp-lobby     multiplayer Random map: Custom Game's Random map... screen (screenshot), the saved
               open-start map selected in the host's map list, the lobby seating the host, a
               joining guest and two AI on four starts with teams (screenshot), the guest's game
               info and roster through the wire format, and both peers starting the match: every
               house on its own start with that start's contents, same state digest after a minute
  legacy-map   a bundled city map without zone levels still starts every lot empty
  vanilla-1v1  small vanilla 1v1
  allied-2v3   2v3 against allied enemies, Large bases, 10k armies, plus a one-minute smoke run
  city-192     DuneCity 192x192, 15k cities at tech 8, measured population, smoke run, save/load
  stefan-128   Stefan's played configuration (128, 2 allies + 2 enemies, tech 9, Medium bases,
               5k armies, 40k cities, seed 488631365): measured population, smoke run, save/load
  partner-support  the player's own Medium base, 5k army and 5k city with a qBotSupportMedium
               partner in the house: two players, base on positive power, in-game screenshots,
               smoke run
  partner-quantbot a full qBotMedium partner in the player's house loads
  partner-fighter  a qBotFighterMedium partner plays a minute with the human idle: nothing is
               queued, upgraded or placed in the player's house (docs/ai-roles.md)
  spectate     the screen's Spectate (DuneCity 96, Ordos + 1 ally + 2 enemies, qBotHard with a
               qBotSupportMedium partner): Start's init is a local-spectator one with no human,
               the observed match has only AI houses and runs a minute (screen and in-game
               screenshots); the choice is remembered and Spectate off seats the human again
  render       whole-map pictures (4 px per tile) and terrain measurements of map-editor random
               maps and of generated maps; --render-save adds the map a saved match started from
  civ-menu     Civilization Wars settings, remembered seed, generic multiplayer starts/lobby/wire rules
  civ-1v1      fixed kit, fifteen turrets/base (ten front), positive full-cost city income,
               original research deadline and at least ten game minutes with all QuantBot seats
  civ-2v2      the same four-house match and mirrored lane setup
  civ-3v3      the same six-house match and one lane per player
  civ-research human selected from Civ Wars menu: both legacy full-price rejection and
               zero-credit installment acceptance, normal income, real SDL Lab clicks and tech-2 completion

Every match case asserts houses/teams, players per house, structure and unit counts, army value
and power against the generator's own plan, and draws the loaded (and played) map. A smoke run
fails on any error, invalid entry or occupied position the engine logs while it plays.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import managed_profile, require_free_space, scratch_dir
sys.path.insert(0, str(root / 'tests/ai'))
from source_provenance import source_fingerprint
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--cases', default='menu,mp-lobby,legacy-map,vanilla-1v1,allied-2v3,city-192,stefan-128,'
                                       'partner-support,partner-quantbot,partner-fighter,spectate,render')
parser.add_argument('--prepare-only', action='store_true', help='Compile and seal the private fixture without running cases')
parser.add_argument('--fixture-dir', type=Path, help='Prepared fixture directory; runtime fails closed if missing or stale')
parser.add_argument('--size', metavar='WxH', help='Run one of the selected case’s existing sizes')
parser.add_argument('--research-payment', choices=('off', 'on'), help='Run one existing civ-research payment state')
parser.add_argument('--keep', action='store_true', help='Keep profiles and saves (screenshots are always kept)')
parser.add_argument('--render-save', type=Path, help='render: also draw the map this saved match (.dls) started from')
parser.add_argument('--render-tag', default='current', help='render: file-name prefix of the generated maps')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
cases = args.cases.split(',')
known_cases = {'menu', 'mp-lobby', 'legacy-map', 'vanilla-1v1', 'allied-2v3', 'city-192', 'stefan-128',
               'partner-support', 'partner-quantbot', 'partner-fighter', 'spectate', 'render',
               'civ-menu', 'civ-1v1', 'civ-2v2', 'civ-3v3', 'civ-research'}
if not cases or any(case not in known_cases for case in cases):
    parser.error('--cases must name existing probe cases')
if args.research_payment and cases != ['civ-research']:
    parser.error('--research-payment requires --cases civ-research')
selected_size = None
if args.size:
    try:
        selected_size = tuple(int(value) for value in args.size.lower().split('x'))
    except ValueError:
        parser.error('--size must be WxH')
    if len(selected_size) != 2:
        parser.error('--size must be WxH')
require_free_space(sweep='if-low')
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
production_link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
production_link = production_link[production_link.index('&&') + 1:]
production_link = production_link[:production_link.index('&&')]


def file_hash(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fixture_identity(source_cache):
    # Commands identify the ABI/toolchain flags; hash the actual linked objects,
    # libraries and configured build too, not just the production executable.
    inputs = {build / target, build / 'CMakeCache.txt', build / 'build.ninja'}
    inputs.update(build / arg for arg in production_link if arg.endswith(('.o', '.a', '.dylib')))
    for line in commands:
        compiler = shlex.split(line)
        if ' -c ' in line:
            for arg in compiler[:compiler.index('-c')]:
                if Path(arg).is_absolute() and Path(arg).is_file() and os.access(arg, os.X_OK):
                    inputs.add(Path(arg))
    return {'source': source_fingerprint(root, build, out, cache=source_cache), 'commands': commands,
            'inputs': {str(path): file_hash(path) for path in sorted(inputs)}}


def compile_fixture(work):
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed.')
    main = main.replace(needle, 'int menuResult = runSkirmishProbe();')
    clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
    if main.count(clamp) != 1:
        raise RuntimeError('Window-size fixture injection point changed.')
    main = main.replace(clamp, '(void)displayIndex;')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = main[:position] + '#include "' + str(root / 'tests/skirmish/skirmish-probe.inc') + '"\n' + main[position:]
    source, obj = work / 'skirmish-main.cpp', work / 'skirmish-main.o'
    source.write_text(main)
    menu_source, menu_obj = work / 'civ-wars-menu.cpp', work / 'civ-wars-menu.o'
    menu = (root / 'src/Menu/CivWarsMenu.cpp').read_text()
    launcher = 'startSinglePlayerGame(init);'
    if menu.count(launcher) != 1:
        raise RuntimeError('Civ Wars game-launch boundary changed.')
    menu_source.write_text('#include <GameInitSettings.h>\n'
                           'void startCivWarsProbeGame(const GameInitSettings&);\n'
                           + menu.replace(launcher, 'startCivWarsProbeGame(init);'))
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
    menu_compile = compile_command.copy()
    menu_compile[menu_compile.index('-o') + 1] = str(menu_obj)
    menu_compile[menu_compile.index('-c') + 1] = str(menu_source)
    app = work / 'skirmish-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/skirmish-probe'
    link = production_link.copy()
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    link = [str(menu_obj) if arg.endswith('/Menu/CivWarsMenu.cpp.o') else arg for arg in link]
    with (work / 'build.log').open('w') as log:
        for phase, command in (('main', compile_command), ('civ-menu', menu_compile), ('link', link)):
            print('SKIRMISH_FIXTURE_PHASE: ' + phase, flush=True)
            subprocess.run(['nice', '-n', '10'] + command, cwd=build, stdout=log,
                           stderr=subprocess.STDOUT, check=True)


fixture_root = args.fixture_dir.resolve() if args.fixture_dir else out / 'fixture'
fixture_root.mkdir(parents=True, exist_ok=True)
with (fixture_root / 'publication.lock').open('a+b') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    cache_path = fixture_root / 'source-fingerprint.json'
    try:
        cache_text = cache_path.read_text()
        source_cache = json.loads(cache_text)
    except (OSError, ValueError):
        cache_text, source_cache = None, {}
    if not isinstance(source_cache, dict):
        source_cache = {}
    identity = fixture_identity(source_cache)
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    fixture = fixture_root / key
    executable = Path('skirmish-probe.app/Contents/MacOS/skirmish-probe')
    if not fixture.exists():
        if args.fixture_dir and not args.prepare_only:
            raise RuntimeError('Missing current skirmish fixture; run --prepare-only with the same --fixture-dir')
        staged = Path(tempfile.mkdtemp(prefix=key + '.incoming-', dir=fixture_root))
        try:
            compile_fixture(staged)
            if fixture_identity(source_cache) != identity:
                raise RuntimeError('Source or build inputs changed during fixture preparation')
            (staged / 'manifest.json').write_text(json.dumps(
                {'identity': identity, 'binary': file_hash(staged / executable)}, sort_keys=True) + '\n')
            staged.rename(fixture)
        except BaseException:
            print('Kept incomplete fixture evidence: ' + str(staged), file=sys.stderr)
            raise
    manifest = json.loads((fixture / 'manifest.json').read_text())
    binary = fixture / executable
    if fixture.is_symlink() or binary.is_symlink() or manifest['identity'] != identity or manifest['binary'] != file_hash(binary):
        raise RuntimeError('Skirmish fixture seal differs; refusing stale or modified executable')
    source_cache_text = json.dumps(source_cache, sort_keys=True) + '\n'
    if source_cache_text != cache_text:
        temporary_cache = None
        try:
            with tempfile.NamedTemporaryFile('w', dir=fixture_root, prefix='source-fingerprint-',
                                             suffix='.incoming', delete=False) as stream:
                temporary_cache = Path(stream.name)
                stream.write(source_cache_text)
            temporary_cache.replace(cache_path)
        finally:
            if temporary_cache is not None:
                temporary_cache.unlink(missing_ok=True)
if args.prepare_only:
    print('SKIRMISH_FIXTURE_READY: ' + str(fixture), flush=True)
    sys.exit(0)

# Engine diagnostics that mean a generated map or match is broken. Occupied
# positions/tiles are invalid placements; a busy refinery is normal gameplay.
problem = re.compile(r'invalid|occupied\s+(?:position|tile)|error|exception|failed|cannot|unknown', re.IGNORECASE)
# Pre-existing startup lines unrelated to the match (mod/asset discovery in an empty profile).
startup_noise = re.compile(r'Error loading|Warning: could not|libpng', re.IGNORECASE)

def run_cases(work):
    summary = []
    for case in cases:
        sizes = ((640, 480), (1280, 720)) if case in ('menu', 'civ-menu', 'civ-research', 'civ-1v1', 'civ-2v2', 'civ-3v3') else ((1024, 768),)
        if selected_size and selected_size not in sizes:
            parser.error('--size is not an existing size for ' + case)
        payments = (args.research_payment,) if args.research_payment else ('off', 'on')
        runs = [(width, height, payment) for width, height in sizes if not selected_size or (width, height) == selected_size
                for payment in (payments if case == 'civ-research' else (None,))]
        for width, height, payment in runs:
            label = case if len(sizes) == 1 else f'{case}-{width}'
            if payment is not None:
                label += '-' + payment
            case_out = out / label if payment is not None else out
            case_out.mkdir(exist_ok=True)
            profile = work / ('profile-' + label) / 'profile'
            profile.mkdir(parents=True)
            (profile / 'Dune City.ini').write_text(
                f'[Video]\nPhysical Width = {width}\nPhysical Height = {height}\nWidth = {width}\nHeight = {height}\n'
                f'Interface Height = {height}\nFullscreen = false\n[General]\nPlay Intro = false\nPlayer Name = Skirmish tester\n')
            env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SKIRMISH_PROBE_OUT=str(case_out), SKIRMISH_PROBE_CASE=case, SKIRMISH_SOURCE_DIR=str(root))
            if payment is not None:
                env['SKIRMISH_RESEARCH_INSTALLMENTS'] = str(int(payment == 'on'))
            if case in ('allied-2v3', 'city-192', 'stefan-128', 'partner-support'):
                env['SKIRMISH_PROBE_SMOKE'] = '1'
            if case == 'render':
                env['SKIRMISH_RENDER_TAG'] = args.render_tag
                if args.render_save:
                    env['SKIRMISH_RENDER_SAVE'] = str(args.render_save.resolve())
            logpath = out / f'run-{label}.log'
            print('SKIRMISH_RUNTIME_PHASE: ' + label, flush=True)
            with managed_profile(profile), logpath.open('w') as log:
                subprocess.run(['nice', '-n', '10', str(binary), '--window', '--showlog'], cwd=work, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=600)
            text = logpath.read_text(errors='replace')
            if text.count('SKIRMISH_PROBE_PASS:') != 1:
                raise RuntimeError(f'{label}: no pass line; see {logpath}')
            if 'SKIRMISH_SMOKE_BEGIN' in text:
                played = text.split('SKIRMISH_SMOKE_BEGIN', 1)[1].split('SKIRMISH_SMOKE_END', 1)[0]
                bad = [line for line in played.splitlines()[1:] if problem.search(line) and not startup_noise.search(line)]
                if bad:
                    raise RuntimeError(f'{label}: {len(bad)} problem line(s) in the played minute, first: {bad[0]}')
            # mp-lobby and spectate report the menus before their match loads: scan up to the match instead.
            marker = {'mp-lobby': 'digest at cycle', 'spectate': 'spectated match loaded'}.get(case, 'SKIRMISH_PROBE:')
            loaded = text.split(marker, 1)[0] if marker in text else text
            map_warnings = [line for line in loaded.splitlines()
                            if re.search(r'- (Random|Civilization Wars).*?\d+:|Invalid or occupied|Invalid (unit|building|house|position)|refused zone', line)]
            if map_warnings:
                raise RuntimeError(f'{label}: map load warnings, first: {map_warnings[0]}')
            summary += [line[line.index('SKIRMISH_PROBE'):] for line in text.splitlines() if 'SKIRMISH_PROBE' in line]
            summary += [line[line.index('SKIRMISH_SMOKE'):] for line in text.splitlines() if 'SKIRMISH_SMOKE' in line]
            summary += [line[line.index('SKIRMISH_RENDER'):] for line in text.splitlines() if 'SKIRMISH_RENDER' in line]
            summary += [line[line.index('CIV_HUMAN_RESEARCH'):] for line in text.splitlines() if 'CIV_HUMAN_RESEARCH' in line]
            if args.keep:
                shutil.copytree(profile, out / ('profile-' + label), dirs_exist_ok=True)
    (out / 'summary.txt').write_text('\n'.join(summary) + '\n')
    print('\n'.join(summary))


with scratch_dir('skirmish-probe') as work:
    run_cases(work)
if not args.keep:
    for save in out.glob('*.dls'):
        save.unlink()
