#!/usr/bin/env python3
"""Run real campaign AI with a fixed seed, isolated profile and no human commands.

Uses the existing Ninja build (macOS: clang; Windows: MSVC objects, with the probe's
main compiled by clang-cl). Access-control relaxation is restricted to
this diagnostic executable; no test hooks are compiled into the shipped game.
--attack-percent sets the legacy Easy fraction (zero disables dispatch); campaign
Easy/Medium use the half-army policy plus per-house wave caps. Results
and structured AI decision logs remain in --output-dir; when the run ends
(passed or failed) the profile's regenerated bulk (bundled mods, captured
workshop revisions) is deleted unless --keep-profile-assets. This is a simulation
comparison, not a substitute for the browser playtest or human playtesting.
"""
import argparse
import atexit
import ctypes
import gzip
import configparser
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
WINDOWS = sys.platform == 'win32'
if not WINDOWS:
    import fcntl

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import copy_revisions, discard_profile_assets, prune_runs  # noqa: E402


def windows_argv(command):
    """Split a Windows command line exactly as the MSVC runtime does."""
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
    """main.cpp's compile command, retargeted to clang-cl (MSVC has no -fno-access-control;
    clang-cl objects are ABI-compatible with the MSVC ones), the game's link command and its
    output directory. Placeholders as for macOS; @STAGING@ takes the link's side outputs."""
    database = json.loads(subprocess.check_output(['ninja', '-C', str(build), '-t', 'compdb', '-x'], text=True))
    def output(entry):
        return entry.get('output', '').replace('\\', '/')
    main_object = '/dunecity.dir/main.cpp.obj'   # src/CMakeFiles/dunecity.dir/main.cpp.obj
    compile_entry = next(e for e in database if ('/' + output(e)).endswith(main_object))
    link_entry = next(e for e in database if output(e).endswith('/dunecity.exe') and 'vs_link_exe' in e['command'])
    compile_command = windows_argv(compile_entry['command'])
    if Path(compile_command[0]).name.lower().startswith('sccache'):
        compile_command = compile_command[1:]
    clang = os.environ.get('DUNECITY_CLANG_CL') or shutil.which('clang-cl')
    if not clang:
        # Visual Studio's "C++ Clang tools": VC/Tools/Llvm beside VC/Tools/MSVC/<version>/bin/Hostx64/x64/cl.exe.
        clang = str(Path(compile_command[0]).parents[5] / 'Llvm/x64/bin/clang-cl.exe')
    if not Path(clang).is_file():
        raise RuntimeError('clang-cl not found; install the Visual Studio C++ Clang tools or set DUNECITY_CLANG_CL.')
    command = [clang]
    for arg in compile_command[1:]:
        if arg == '/showIncludes' or arg.startswith('/Fd'):
            continue
        command.append('/Fo@OBJECT@' if arg.startswith('/Fo') else arg)
    command[command.index(next(a for a in command if a in ('-c', '/c'))) + 1] = '@SOURCE@'
    command += ['-Xclang', '-fno-access-control']
    # cmd.exe /C "cd . && cmake -E vs_link_exe ... -- link.exe ... && <post-build steps>": only the link.
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
            arg = '/implib:@STAGING@\\balance-probe.lib'
        elif lower.startswith('/pdb:'):
            arg = '/pdb:@STAGING@\\balance-probe.pdb'
        elif arg.replace('\\', '/').endswith(main_object):
            arg = '@OBJECT@'
        link.append(arg)
    if '@OBJECT@' not in link or '/out:@BINARY@' not in link:
        raise RuntimeError('Unexpected Windows link command for dunecity.exe.')
    return command, link, (build / output(link_entry)).parent


def lock_exclusive(handle):
    if WINDOWS:
        import msvcrt
        while True:
            try:
                msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
                return
            except OSError:
                continue   # LK_LOCK gives up after ten seconds; keep waiting
    fcntl.flock(handle, fcntl.LOCK_EX)


house_names = ('harkonnen','atreides','ordos','fremen','sardaukar','mercenary','neutral','rebels','custom','wildspade','kleshmersh','tharpique')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--quantbot-object', type=Path, nargs='+',
                    help='Diagnostic-only policy comparison: link preserved QuantBot object(s) in place of '
                         'QuantBot.cpp and its subsystem files')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--repeatable', action='store_true', help='Keep each CTest run in a fresh subdirectory')
parser.add_argument('--keep-runs', type=int, default=3,
                    help='With --repeatable: how many earlier run-* directories to keep (oldest removed first)')
parser.add_argument('--keep-profile-assets', action='store_true',
                    help="Keep the profile's bundled mods and workshop revisions after a successful run")
parser.add_argument('--custom-map', type=Path, help='Run all occupied slots of a custom map instead of the campaign')
parser.add_argument('--free-for-all', action='store_true', help='Give each custom-map house its own team')
parser.add_argument('--capture-mib', type=int, default=1024, help='Diagnostic capture allowance; shipped default is unchanged')
parser.add_argument('--wall-timeout', type=int, default=1800, help='Maximum wall seconds for the simulation')
parser.add_argument('--level', type=int, choices=range(1,10), default=4)
parser.add_argument('--mod', choices=('vanilla','dunecity','Dune2R','Tornie'), default='vanilla')
parser.add_argument('--house', choices=tuple(h for h in house_names if h!='neutral'), default='harkonnen')
parser.add_argument('--roster', help='Explicit custom-map house:team slots in lobby order, comma-separated')
parser.add_argument('--local-spectator', action='store_true',
                    help='Run a custom-map all-AI match through the real detached spectator setup')
parser.add_argument('--harvester-limit', type=int, choices=range(-1,101), default=-1)
parser.add_argument('--rocket-turrets-need-power', action=argparse.BooleanOptionalAction, default=None)
parser.add_argument('--concrete-required', action=argparse.BooleanOptionalAction, default=None)
parser.add_argument('--require-win', action='store_true', help='Fail a full-match run on defeat or time limit')
parser.add_argument('--game-options', type=Path,
                    help='Apply this INI file\'s [Game Options] section (a player\'s Dune City.ini format) last')
parser.add_argument('--elimination-match', action='store_true',
                    help='Play until one team is left (also as a local spectator) and log per-minute house samples')
parser.add_argument('--ffa-screenshot', type=Path,
                    help='With --elimination-match or --match-sides: save a rendered PNG of --house\'s army (centred '
                         'between its assembly point and its forward bundle) at the first sample, at --viewport')
parser.add_argument('--ffa-screenshot-minute', type=int, help='--ffa-screenshot: at the first sample from this game minute')
parser.add_argument('--load-save', type=Path, help='Continue this savegame instead of starting a new match')
parser.add_argument('--save-profile', type=Path,
                    help='With --load-save: a player profile whose workshop/ holds the revisions the save pins')
parser.add_argument('--partner-difficulty', choices=('easy','medium','hard','brutal'), default='easy')
parser.add_argument('--enemy-ai', choices=('quantbot','ai-player','campaign'), default='quantbot',
                    help='Enemy controller family; AI Player has Easy/Medium/Hard; campaign = CampaignAIPlayer '
                         '(the campaign menu\'s "scenario settings" enemy, any --enemy-difficulty)')
parser.add_argument('--enemy-difficulty', choices=('easy','medium','hard','brutal'), default='easy')
parser.add_argument('--shared-spending-probe', action='store_true')
parser.add_argument('--controls-probe', action='store_true')
parser.add_argument('--stats-ui-probe', action='store_true',
                    help='Exercise the Stats window, observer house browsing and the sidebar layout')
parser.add_argument('--viewport', default=None, metavar='WIDTHxHEIGHT',
                    help='Render the UI probe at this window size (default 640x480)')
parser.add_argument('--real-video', action='store_true',
                    help='Open a real window with the platform renderer instead of the dummy video driver '
                         '(game-perf.py --rendered: the interactive main loop at --viewport, default 1440x900)')
parser.add_argument('--sourceforge-probe', action='store_true')
parser.add_argument('--preplace-probe', action='store_true',
                    help='Construction Yard pre-placement queue: reservations, chain, fallback, cancel, save/load, 9854 load')
parser.add_argument('--sidebar-grid-probe', action='store_true',
                    help='Sidebar build grid: yard, factory and Starport lists rendered at --viewport (PNG screenshots)')
parser.add_argument('--harvester-safety-probe', action='store_true')
parser.add_argument('--city-placement-probe', action='store_true')
parser.add_argument('--opening-economy-probe', action='store_true')
parser.add_argument('--starport-probe', action='store_true', help='Exercise reserved cash with above-normal Starport prices')
parser.add_argument('--helper-economy-probe', action='store_true', help='Verify advanced campaign helper worker investment and paid imports')
parser.add_argument('--city-campaign-probe', action='store_true', help='Verify campaign city limits, permissions, depletion and save/load')
parser.add_argument('--no-population-victory-probe', action='store_true',
                    help='Verify a WinFlags=19/23 campaign mission is not won by city population')
parser.add_argument('--spice-income-probe', action='store_true',
                    help='Verify the per-house spice delivery income factor against real refinery unloading')
parser.add_argument('--credit-storage-probe', action='store_true',
                    help='Verify the shared spice/tax storage limit, exempt starting cash and refunds')
parser.add_argument('--stats-probe', action='store_true', help='Verify campaign results with a shared human/AI house')
parser.add_argument('--growth-installment-probe', action='store_true',
                    help='Verify that a demanded Stadium/Airport and a planned reactor enter production '
                         'on a start budget and are charged in installments from arriving income')
parser.add_argument('--civic-priority-probe', action='store_true',
                    help='Isolate required civic priority against active optional investment rules')
parser.add_argument('--nuclear-probe', action='store_true')
parser.add_argument('--reactor-safety-probe', action='store_true')
parser.add_argument('--degradation-probe', action='store_true',
                    help='Verify Dynasty-aligned power damage, foundation decay and their save state')
parser.add_argument('--radar-probe', action='store_true')
parser.add_argument('--army-probe', action='store_true')
parser.add_argument('--path-workers', type=int, default=0,
                    help='Pin the pathfinding worker count (0 leaves the engine default). '
                         'Used to show that a probe reaches the same result under one and '
                         'four workers.')
parser.add_argument('--recovery-probe', action='store_true',
                    help='Verify Custom Hard/Brutal army recovery, wave cohesion, the outnumbered '
                         'dispatch gate, enemy-facing batteries and the special-unit allocator')
parser.add_argument('--battery-clearance-probe', action='store_true',
                    help='Verify crowded-city rocket batteries replace eligible R/I/C lots through real production')
parser.add_argument('--air-defense-probe', action='store_true')
parser.add_argument('--police-placement-probe', action='store_true')
parser.add_argument('--police-budget-probe', action='store_true')
parser.add_argument('--police-reinforcement-probe', action='store_true',
                    help='Verify police reinforcements follow the selected unit limit, not a police-local ceiling')
parser.add_argument('--city-growth-probe', action='store_true',
                    help='Verify continuous city growth while proactive defence/supplier goals are unmet')
parser.add_argument('--no-spice-city-probe', action='store_true',
                    help='Verify a city map with no usable spice seeds demanded R/C/I out of its '
                         'starting grant instead of refinery/port/transport capital, across power '
                         'starvation, bankruptcy, parallel yards, depletion and save/load')
parser.add_argument('--mcv-deployment-probe', action='store_true',
                    help='Verify MCVs deploy on the rock the base already holds instead of driving around it')
parser.add_argument('--custom-attack-probe', action='store_true')
parser.add_argument('--whole-army-probe', action='store_true',
                    help='Verify the Custom Hard/Brutal whole-army dispatch and asset emergency defence')
parser.add_argument('--launcher-kiting-probe', action='store_true',
                    help='Verify hunting launchers escape a too-close enemy while keeping Hunt')
parser.add_argument('--formation-measure', action='store_true',
                    help='Measure attack-wave arrival spread and first-contact shots over the match')
parser.add_argument('--wave-formation',
                    help='QuantBot attack-wave formation for this run, by its INI name (default: the QuantBot Config.ini value)')
parser.add_argument('--wave-formation-probe', action='store_true',
                    help='Verify QuantBot attack waves launch and march as hunting formations in the configured shape')
parser.add_argument('--blob-wave-probe', action='store_true',
                    help='Verify QuantBot hunt parties (WaveFormation Blob) gather, march, release, reform and fall back')
parser.add_argument('--force-probe', action='store_true',
                    help='Verify the QuantBot force-concentration defaults: focus fire, converging on a fight, 60%% waves')
parser.add_argument('--strategy-probe', action='store_true',
                    help='Verify QuantBot strategies: peers and reloads agree on the pick, mixes follow their priors '
                         '(needs --custom-map, a multi-house map, for the lockstep and save/load checks)')
parser.add_argument('--duel-map-size', type=int, choices=(64, 96, 128, 160, 192, 256),
                    help='Formation duel: a generated mirrored Random Skirmish 1v1 of this size, Fortress bases and '
                         '20,000-credit armies, two QuantBots (--enemy-difficulty) until one is eliminated')
parser.add_argument('--duel-map-seed', type=int, default=1, help='Formation duel: the Random Skirmish map seed')
parser.add_argument('--duel-sides', help='Formation duel: house:shape,house:shape (e.g. atreides:box,harkonnen:wedge)')
parser.add_argument('--match-sides',
                    help='Vanilla match on --custom-map or --skirmish-ffa-size (vanilla-match.py): house:label per map '
                         'slot in slot order, each house its own team; label = a duel label (none, none+BWFCQ, ...) for '
                         'a QuantBot of --enemy-difficulty, or ai for the AI Player of it')
parser.add_argument('--skirmish-ffa-size', type=int, choices=(64, 96, 128, 160, 192, 256),
                    help='Free-for-all on a generated multiplayer Random Skirmish map of this size (vanilla-match.py '
                         '--skirmish): one start per --match-sides house, each a Fortress with a 20,000-credit army and, '
                         'with --mod dunecity, a 40,000 city; Rock High, Spice Medium, not mirrored - the settings of '
                         "Stefan's 4P DuneCity free-for-all")
parser.add_argument('--skirmish-map-seed', type=int, default=1, help='--skirmish-ffa-size: the Random Skirmish map seed')
parser.add_argument('--prebuilt-probe', type=Path,
                    help='Run this balance-probe binary (from an earlier run of this tree) instead of building one')
parser.add_argument('--no-ai-telemetry', action='store_true',
                    help='Batch measurement: no AI decision log (DUNECITY_AI_TELEMETRY=0), so no events.jsonl, no '
                         'telemetry in summary.json and no telemetry-based wave-budget check. The simulation is the '
                         'same; formation-duel.py and headless-bench.py use it (docs/headless-performance.md). '
                         'Otherwise DUNECITY_AI_TELEMETRY=1 turns the log on: games default to [General] AI Decision '
                         'Log = false')
parser.add_argument('--factory-recovery-probe', action='store_true')
parser.add_argument('--strategic-site-probe', action='store_true',
                    help='Continue the stalled-city fixture with its overdue Palace timer restored')
parser.add_argument('--pressure-probe', action='store_true', help='Verify campaign wave readiness, survivor independence and save state')
parser.add_argument('--defence-probe', action='store_true', help='Verify retaliation and base/harvester reinforcements')
parser.add_argument('--repair-probe', action='store_true', help='Verify experienced campaign bots replace missing repair yards')
parser.add_argument('--pacing-probe', action='store_true', help='Verify enemy worker caps and small-wave readiness')
parser.add_argument('--scenario', action='store_true',
                    help='AI scenario matrix game (tests/ai/scenario-matrix.py, scenario-match.inc): a campaign mission '
                         'to its own end, or with --scenario-sides a match on --custom-map/--skirmish until one team is left')
parser.add_argument('--scenario-sides',
                    help='Scenario match: house:team:class per house (class as PlayerFactory names it, e.g. '
                         'atreides:2:qBotBrutal,harkonnen:1:AIPlayerHard); on --custom-map one per map slot')
parser.add_argument('--skirmish',
                    help='Scenario match on a generated Random Skirmish multiplayer map, one start per side: '
                         'key=value,... (size, base none|cy|outpost|small|medium|large|fortress, army, city, seed, '
                         'tech, rock, spice, credits, symmetric; scenario-match.inc)')
parser.add_argument('--mission', type=int, choices=range(1, 23),
                    help='Campaign mission number instead of the --level\'s first (levels 2-8 have three each)')
parser.add_argument('--seed', type=int, default=486409243)
parser.add_argument('--minutes', type=int, default=20)
parser.add_argument('--attack-percent', type=int, choices=range(101), default=25)
args = parser.parse_args()
if not 1 <= args.minutes <= 120 or not 0 <= args.seed <= 0xffffffff:
    parser.error('Use 1–120 minutes and a 32-bit unsigned seed.')
if not 256 <= args.capture_mib <= 4096 or args.wall_timeout < 1:
    parser.error('Use 256–4096 MiB of capture space and a positive wall timeout.')
if args.free_for_all and not args.custom_map:
    parser.error('--free-for-all requires --custom-map.')
if args.local_spectator and not (args.custom_map or args.duel_map_size or args.skirmish_ffa_size or args.skirmish):
    parser.error('--local-spectator requires --custom-map, --duel-map-size, --skirmish-ffa-size or --skirmish.')
if args.load_save and (args.custom_map or args.duel_map_size or args.skirmish_ffa_size or not args.load_save.is_file()):
    parser.error('--load-save needs an existing savegame and no --custom-map, --duel-map-size or --skirmish-ffa-size.')
if args.game_options and not args.game_options.is_file():
    parser.error('--game-options must name an existing INI file.')
if bool(args.duel_map_size) != bool(args.duel_sides) or (args.duel_map_size and args.custom_map):
    parser.error('A formation duel needs --duel-map-size and --duel-sides and no --custom-map.')
if args.skirmish_ffa_size and (args.custom_map or args.duel_map_size or not args.match_sides):
    parser.error('--skirmish-ffa-size needs --match-sides and no --custom-map or --duel-map-size.')
if args.match_sides and (not (args.custom_map or args.skirmish_ffa_size) or args.roster or args.free_for_all):
    parser.error('--match-sides needs --custom-map or --skirmish-ffa-size and replaces --roster/--free-for-all.')
if args.quantbot_object and not all(path.is_file() for path in args.quantbot_object):
    parser.error('--quantbot-object must name existing object files.')
if args.enemy_ai == 'ai-player' and args.enemy_difficulty == 'brutal':
    parser.error('AI Player has no Brutal controller.')
if args.strategy_probe and (not args.custom_map or args.prebuilt_probe):
    parser.error('--strategy-probe needs --custom-map and builds its own probe binary.')
if not (args.custom_map or args.skirmish_ffa_size or args.skirmish) and (args.roster or args.house not in house_names[:3]):
    parser.error('Explicit rosters and additional houses require --custom-map, --skirmish-ffa-size or --skirmish.')
def parse_match_sides(slots):
    """--match-sides as [(house index, label)], one distinct house per slot (any 2-8 if slots is None)."""
    try:
        parsed = [(house_names.index(house.lower()), label) for house, label in
                  (side.split(':', 1) for side in args.match_sides.split(','))]
    except ValueError:
        parser.error('--match-sides must be house:label,house:label with known houses.')
    if (slots is not None and len(parsed) != slots) or not 2 <= len(parsed) <= 8 or len({h for h, _ in parsed}) != len(parsed):
        parser.error(f'--match-sides needs one distinct house per map slot ({slots or "2-8"}).')
    if any(label == 'ai' for _, label in parsed) and args.enemy_difficulty == 'brutal':
        parser.error('AI Player has no Brutal controller.')
    if house_names.index(args.house) not in {h for h, _ in parsed}:
        parser.error('--house must be one of the --match-sides houses.')
    return parsed
scenario_sides = []
if args.scenario_sides:
    try:
        scenario_sides = [(house_names.index(house.lower()), int(team), player_class) for house, team, player_class in
                          (side.split(':') for side in args.scenario_sides.split(','))]
    except ValueError:
        parser.error('--scenario-sides must be house:team:class,... with known houses.')
    if (len({h for h, _, _ in scenario_sides}) != len(scenario_sides) or any(t < 1 for _, t, _ in scenario_sides)
            or len({t for _, t, _ in scenario_sides}) < 2):
        parser.error('--scenario-sides needs distinct houses, teams from 1 and at least two teams.')
    if house_names.index(args.house) not in {h for h, _, _ in scenario_sides}:
        parser.error('--house must be one of the --scenario-sides houses.')
if (args.scenario_sides or args.skirmish or args.mission) and not args.scenario:
    parser.error('--scenario-sides, --skirmish and --mission need --scenario.')
if args.scenario:
    if args.duel_map_size or args.match_sides or args.elimination_match or args.roster or args.free_for_all or args.load_save:
        parser.error('--scenario replaces --duel-map-size, --match-sides, --elimination-match, --roster, '
                     '--free-for-all and --load-save.')
    if bool(args.custom_map or args.skirmish) != bool(args.scenario_sides) or (args.custom_map and args.skirmish):
        parser.error('A scenario match needs --scenario-sides and exactly one of --custom-map and --skirmish; '
                     'a campaign scenario neither.')
    if args.mission and args.scenario_sides:
        parser.error('--mission is for campaign scenarios.')


def team_number(brain, default):
    """Brain=TeamN -> N (trailing junk ignored), else default."""
    digits = brain[4:].split()[0] if brain.startswith('team') and brain[4:].split() else ''
    return int(digits) if digits.isdigit() else default


roster = []
sides = []
if args.custom_map:
    scenario = configparser.ConfigParser(strict=False, interpolation=None)
    # Map files are bytes to the game; a few bundled ones (6P Full Wormage) carry binary junk
    # between their sections, which the game's INI reader skips. Read only the INI lines.
    lines = args.custom_map.read_text(encoding='utf-8', errors='replace').splitlines()
    scenario.read_string('\n'.join(line for line in lines if line.replace('\t', ' ').isprintable()
                                   and (line.strip().startswith(('[', ';', '#')) or '=' in line or not line.strip())))
    sections = {s.lower(): s for s in scenario.sections()}
    names = house_names
    assigned = set()
    for name in names:
        if name in sections:
            brain = scenario[sections[name]].get('brain', 'Team2').lower()
            team = team_number(brain, 1 if name == args.house else 2)
            roster.append((names.index(name), team))
            assigned.add(name)
    # Generic slots use the chosen player house first, then distinct opponents.
    # The engine still chooses their spawn slots using the supplied match seed.
    # Keep the existing six-house assignment order, then include the additional
    # factions supported by the lobby. Large user maps have Player7/Player8.
    candidates = [args.house] + [n for n in ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary',
                                           'neutral', 'rebels', 'custom', 'wildspade', 'kleshmersh', 'tharpique')
                                     if n != args.house]
    for slot in range(1, len(house_names) + 1):
        section = sections.get(f'player{slot}')
        if not section:
            continue
        name = next((n for n in candidates if n not in assigned), None)
        if name is None:
            parser.error('Map has more occupied slots than supported houses.')
        brain = scenario[section].get('brain', f'Team{slot}').lower()
        team = team_number(brain, slot)
        roster.append((names.index(name), team))
        assigned.add(name)
    if args.house not in assigned:
        parser.error('Chosen player house has no slot on this map.')
    if args.roster:
        try:
            requested = [(names.index(name), int(team)) for name,team in
                         (slot.split(':') for slot in args.roster.lower().split(','))]
        except ValueError:
            parser.error('Roster must contain known house:team entries.')
        if (len(requested)!=len(roster) or len({h for h,_ in requested})!=len(requested)
                or any(t<1 for h,t in requested)
                or names.index(args.house) not in {h for h,_ in requested}):
            parser.error('Roster must match map slot count, use distinct playable houses and include --house.')
        roster=requested
    if args.free_for_all:
        roster = [(house, i + 1) for i, (house, _) in enumerate(roster)]
    if args.match_sides:
        sides = parse_match_sides(len(roster))
        roster = [(house, i + 1) for i, (house, _) in enumerate(sides)]
    if scenario_sides:
        # The map's own house sections (e.g. All against Atreides) must be played by those houses.
        named = {names.index(name) for name in names if name in sections}
        if len(scenario_sides) != len(roster) or not named <= {h for h, _, _ in scenario_sides}:
            parser.error(f'--scenario-sides needs one distinct house per map slot ({len(roster)}), '
                         'including every house the map names.')
        roster = [(house, team) for house, team, _ in scenario_sides]
elif scenario_sides:
    roster = [(house, team) for house, team, _ in scenario_sides]
if args.skirmish_ffa_size:
    sides = parse_match_sides(None)
    roster = [(house, i + 1) for i, (house, _) in enumerate(sides)]
build, out = args.build_dir.resolve(), args.output_dir.resolve()
# Provenance only (summary.json): a worktree whose git directory cannot be read still runs.
try:
    source_commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True,stderr=subprocess.DEVNULL).strip()
    source_modified = bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True,
                                                   stderr=subprocess.DEVNULL).strip())
except subprocess.CalledProcessError:
    source_commit, source_modified = 'unavailable', None
if args.repeatable:
    out.mkdir(parents=True, exist_ok=True)
    # CTest reruns used to accumulate every run (~80 MB each) forever.
    prune_runs(out, 'run-', max(0, args.keep_runs - 1))
    out = Path(tempfile.mkdtemp(prefix='run-', dir=out))
else:
    out.mkdir(parents=True, exist_ok=False)
if not args.keep_profile_assets:
    # Success or failure, the profile's regenerated bulk is never evidence; logs, saves,
    # replays and the AI decision log stay.
    atexit.register(discard_profile_assets, out/'profile')
policy_comparison = None
if args.prebuilt_probe:
    # A probe binary built by an earlier run of this script from the same tree (formation-duel.py
    # builds one and runs hundreds of matches with it).
    binary = args.prebuilt_probe.resolve()
    if not binary.is_file():
        parser.error('--prebuilt-probe must name an existing balance-probe binary.')
else:
    subprocess.run([sys.executable,str(root/'scripts/check-build-deps.py'),str(build)],check=True,cwd=root)
    main = (root/'src/main.cpp').read_text(encoding='utf-8')
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed.')
    main = main.replace(needle,'int menuResult = runQuantBotStrategyProbe();' if args.strategy_probe
                        else 'int menuResult = runCampaignBalanceProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
    if args.stats_ui_probe or args.sidebar_grid_probe:
        # Exercise the requested logical viewport even on a smaller/headless host.
        # Only this private probe bypasses the ordinary desktop-fit clamp; the
        # shipped application retains it. SDL still supplies the real renderer.
        fit_call = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
        if main.count(fit_call) != 1:
            raise RuntimeError('Window-size fixture hook changed.')
        main = main.replace(fit_call, '/* Stats renderer fixture keeps its requested viewport. */')
        main = main.replace('                              videoFlags);',
                            '                              videoFlags | SDL_WINDOW_HIDDEN);')
    position = main.index('int main(')
    probe_includes = ['tests/ai/windows-probe-compat.inc'] if WINDOWS else []
    probe_includes.append('tests/ai/campaign-balance-probe.inc')
    if args.strategy_probe: probe_includes.append('tests/ai/strategy-probe.inc')
    main = main[:position] + ''.join('#include "'+(root/name).as_posix()+'"\n' for name in probe_includes) + main[position:]
    # Placeholders keep the commands, and so the cache key below, independent of --output-dir.
    if WINDOWS:
        compile_command, link, game_dir = windows_probe_commands(build)
        object_suffix, link_inputs = '.obj', ('.obj', '.lib')
    else:
        target = 'bin/dunecity.app/Contents/MacOS/dunecity'
        commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
        compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
        for option in ('-include','-MT','-MF'):
            if option in compile_command:
                position = compile_command.index(option)
                del compile_command[position:position+2]
        for option in ('-MD','-MMD'):
            if option in compile_command: compile_command.remove(option)
        compile_command[compile_command.index('-o')+1] = '@OBJECT@'
        compile_command[compile_command.index('-c')+1] = '@SOURCE@'
        compile_command.append('-fno-access-control')
        link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
        link = link[link.index('&&')+1:]
        link = link[:link.index('&&')]
        link[link.index('-o')+1] = '@BINARY@'
        link = ['@OBJECT@' if arg.endswith('/main.cpp.o') else arg for arg in link]
        object_suffix, link_inputs = '.o', ('.o', '.a', '.dylib', '.tbd')
    if args.quantbot_object:
        # QuantBot.cpp and its subsystem files implement one class. The preserved
        # object(s), from a build before or after that split, replace all of them.
        split = [f'/players/QuantBot{part}.cpp{object_suffix}' for part in
                 ('', 'SaveLoad', 'Colonisation', 'Build', 'Placement', 'Army', 'Blob', 'Air', 'Harvesters')]
        present = [arg for arg in link if any(arg.replace('\\', '/').endswith(name) for name in split)]
        if len(present) != len(split):
            raise RuntimeError('Expected the QuantBot subsystem objects in the diagnostic link.')
        preserved = [path.resolve() for path in args.quantbot_object]
        position = link.index(present[0])
        link = [arg for arg in link if arg not in present]
        link[position:position] = [str(path) for path in preserved]
        policy_comparison = [{'object': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                             for path in preserved]

    # Every probe of a build tree compiles the same balance-main.cpp and links it with the
    # same objects, so the binary is cached in the build tree (balance-probe-cache/) and
    # reused while nothing it was made from has changed: the generated source and both
    # commands (the key), and the size and modification time of every file the compiler
    # read (its depfile) and every object and library on the link line (the manifest).
    def stamp(path):
        info = os.stat(path)
        return [info.st_mtime_ns, info.st_size]
    def current(manifest):
        try:
            return all(stamp(path) == value for path, value in json.loads(manifest.read_text()).items())
        except (OSError, ValueError):
            return False
    cache = build/'balance-probe-cache'
    cache.mkdir(exist_ok=True)
    entry = cache/hashlib.sha256(json.dumps([main, compile_command, link]).encode()).hexdigest()[:24]
    executable = 'balance-probe.exe' if WINDOWS else 'balance-probe'
    with open(cache/'.lock', 'w') as lock:
        lock_exclusive(lock)   # concurrent probes build one binary, once
        rebuilt = False
        if not current(entry/'manifest.json'):
            shutil.rmtree(entry, ignore_errors=True)
            staging = Path(tempfile.mkdtemp(prefix='staging-', dir=cache))
            names = {'@SOURCE@': str(staging/'balance-main.cpp'), '@OBJECT@': str(staging/('balance-main'+object_suffix)),
                     '@BINARY@': str(staging/executable), '@STAGING@': str(staging)}
            def fill(arg):
                for placeholder, value in names.items():
                    arg = arg.replace(placeholder, value)
                return arg
            try:
                (staging/'balance-main.cpp').write_text(main, encoding='utf-8')
                with (out/'build.log').open('w') as log:
                    if WINDOWS:
                        # clang-cl reports the headers it read like cl.exe: "Note: including file: <path>".
                        done = subprocess.run([fill(arg) for arg in compile_command] + ['/showIncludes'],
                                              cwd=build,capture_output=True,text=True,errors='replace')
                        log.write(done.stdout + done.stderr)
                        log.flush()
                        if done.returncode:
                            raise subprocess.CalledProcessError(done.returncode, compile_command)
                        prefix = 'Note: including file:'
                        headers = [line[len(prefix):].strip() for line in (done.stdout + done.stderr).splitlines()
                                   if line.startswith(prefix)]
                    else:
                        subprocess.run([fill(arg) for arg in compile_command] + ['-MD', '-MF', str(staging/'deps.d')],
                                       cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
                    subprocess.run([fill(arg) for arg in link],
                                   cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
                if not WINDOWS:
                    depfile = (staging/'deps.d').read_text().replace('\\\n', ' ').split(':', 1)[1]
                    headers = [path for path in shlex.split(depfile) if Path(path).name != 'balance-main.cpp']
                inputs = [Path(build, path).resolve() for path in headers]
                inputs += [Path(build, arg).resolve() for arg in link
                           if arg.lower().endswith(link_inputs) and Path(build, arg).is_file()]
                (staging/'manifest.json').write_text(json.dumps({str(path): stamp(path) for path in inputs}))
                os.rename(staging, entry)
                rebuilt = True
            finally:
                shutil.rmtree(staging, ignore_errors=True)
        else:
            (out/'build.log').write_text(f'Reused {entry}\n')
        os.utime(entry)
        # Keep the four most recently used binaries (other configurations, e.g. --stats-ui-probe).
        stale_entries = sorted((p for p in cache.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime)[:-4]
        for stale in stale_entries:
            shutil.rmtree(stale, ignore_errors=True)
        if WINDOWS:
            # Windows finds the game data beside the executable (SDL_GetBasePath), so the probe
            # runs from the game's own output directory rather than a per-run copy.
            for stale in stale_entries:
                (game_dir/f'balance-probe-{stale.name}.exe').unlink(missing_ok=True)
            binary = game_dir/f'balance-probe-{entry.name}.exe'
            if rebuilt or not binary.is_file():
                shutil.copy2(entry/executable, binary)
    if WINDOWS:
        (out/'balance-probe-path.txt').write_text(str(binary) + '\n')
    else:
        app = out/'balance-probe.app/Contents'
        (app/'MacOS').mkdir(parents=True)
        (app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
        binary = app/'MacOS/balance-probe'
        shutil.copy2(entry/'balance-probe', binary)
    (out/'balance-main.cpp').write_text(main, encoding='utf-8')
env = dict(os.environ,DUNECITY_USERDIR=str(out/'profile'),SDL_AUDIODRIVER='dummy',
           BALANCE_MOD=args.mod,BALANCE_LEVEL=str(args.level),BALANCE_PARTNER=args.partner_difficulty,BALANCE_SEED=str(args.seed),BALANCE_MINUTES=str(args.minutes),
           BALANCE_ATTACK_PERCENT=str(args.attack_percent),BALANCE_ENEMY=args.enemy_difficulty,BALANCE_ENEMY_AI=args.enemy_ai,
           BALANCE_HOUSE=str(house_names.index(args.house)),BALANCE_HARVESTER_LIMIT=str(args.harvester_limit))
if not args.real_video:
    env['SDL_VIDEODRIVER'] = 'dummy'
env['BALANCE_CAPTURE_MIB'] = str(args.capture_mib)
if not args.real_video:
    # The game asks for an accelerated renderer (Direct3D 11 on Windows), which the dummy video
    # driver cannot provide in SDL2. Pin it on the SDL2 Mac release runner too;
    # SDL3 compatibility falling back implicitly is not portable. Real windows
    # retain their native renderer for meaningful rendered performance checks.
    # The environment outranks the game's SDL_SetHint, and a hinted driver is used whatever the flags.
    env['SDL_RENDER_DRIVER'] = 'software'
if args.rocket_turrets_need_power is not None:
    env['BALANCE_ROCKET_TURRETS_NEED_POWER'] = str(int(args.rocket_turrets_need_power))
if args.concrete_required is not None:
    env['BALANCE_CONCRETE_REQUIRED'] = str(int(args.concrete_required))
if args.local_spectator:
    env['BALANCE_LOCAL_SPECTATOR'] = '1'
if args.custom_map:
    env['BALANCE_CUSTOM_MAP'] = str(args.custom_map.resolve())
    env['BALANCE_ROSTER'] = ','.join(f'{house}:{team}' for house, team in roster)
if args.game_options: env['BALANCE_GAME_OPTIONS'] = str(args.game_options.resolve())
if args.elimination_match: env['BALANCE_ELIMINATION_MATCH'] = '1'
if args.load_save:
    env['BALANCE_LOAD_SAVE'] = str(args.load_save.resolve())
    # A player's save pins the Workshop revisions of its mod and map; his profile's
    # workshop/revisions supply them without a metaserver download. Only those
    # revisions are cloned, never the whole store or the publisher's owner token.
    if args.save_profile:
        copy_revisions(args.save_profile, out/'profile', [args.load_save])
(out/'setup.json').write_text(json.dumps({**vars(args), 'resolved_roster': roster,
                                       'policy_comparison': policy_comparison}, default=str, indent=2)+'\n')
if args.shared_spending_probe: env['BALANCE_SHARED_SPENDING_PROBE'] = '1'
if args.harvester_safety_probe: env['BALANCE_HARVESTER_SAFETY_PROBE'] = '1'
if args.sourceforge_probe: env['BALANCE_SOURCEFORGE_PROBE'] = '1'
if args.preplace_probe:
    env['BALANCE_PREPLACE_PROBE'] = '1'
    # A save written by the last 9854 build (origin/main 40f4e7a8): it must still load.
    fixture = out/'preplace-9854.sav'
    fixture.write_bytes(gzip.decompress((root/'tests/ai/preplace-9854.sav.gz').read_bytes()))
    env['BALANCE_PREPLACE_9854_SAVE'] = str(fixture)
if args.sidebar_grid_probe: env['BALANCE_SIDEBAR_GRID_PROBE'] = '1'
if args.ffa_screenshot:
    # A rendered game view (dummy video driver, software renderer) of one house's army at the
    # first per-minute sample of an elimination match (tests/ai/ffa-army.inc).
    env['BALANCE_FFA_SCREENSHOT'] = str(args.ffa_screenshot.resolve())
    if args.ffa_screenshot_minute is not None:
        env['BALANCE_FFA_SCREENSHOT_MINUTE'] = str(args.ffa_screenshot_minute)
    env['BALANCE_FFA_SCREENSHOT_HOUSE'] = str(house_names.index(args.house))
    args.viewport = args.viewport or '1440x900'
if args.real_video and not args.viewport:
    args.viewport = '1440x900'
if (args.controls_probe or args.sourceforge_probe or args.stats_ui_probe or args.preplace_probe or args.sidebar_grid_probe
        or args.real_video or args.ffa_screenshot):
    if args.controls_probe: env['BALANCE_CONTROLS_PROBE'] = '1'
    if args.stats_ui_probe: env['BALANCE_STATS_UI_PROBE'] = '1'
    width, height = 640, 480
    if args.viewport:
        width, height = (int(value) for value in args.viewport.lower().split('x'))
    profile = out/'profile'
    profile.mkdir(exist_ok=True)
    # Native 1:1 so a 1920x1080 window really lays the sidebar out at 1080 lines.
    interface_height = -1 if (args.stats_ui_probe or args.sidebar_grid_probe) else height
    if args.stats_ui_probe:
        env['BALANCE_STATS_EXPECTED_WIDTH'] = str(width)
        env['BALANCE_STATS_EXPECTED_HEIGHT'] = str(height)
    (profile/'Dune City.ini').write_text(f'[Video]\nPhysical Width = {width}\nPhysical Height = {height}\nWidth = {width}\nHeight = {height}\nInterface Height = {interface_height}\nFullscreen = false\n[General]\nPlay Intro = false\n')
if args.city_placement_probe: env['BALANCE_CITY_PLACEMENT_PROBE'] = '1'
if args.opening_economy_probe: env['BALANCE_OPENING_ECONOMY_PROBE'] = '1'
if args.growth_installment_probe or args.civic_priority_probe:
    env['BALANCE_GROWTH_INSTALLMENT_PROBE'] = '1'
if args.civic_priority_probe: env['BALANCE_CIVIC_PRIORITY_ONLY'] = '1'
if args.nuclear_probe or args.reactor_safety_probe: env['BALANCE_NUCLEAR_PROBE'] = '1'
if args.reactor_safety_probe: env['BALANCE_REACTOR_SAFETY_PROBE'] = '1'
if args.degradation_probe: env['BALANCE_DEGRADATION_PROBE'] = '1'
if args.radar_probe: env['BALANCE_RADAR_PROBE'] = '1'
if args.army_probe: env['BALANCE_ARMY_PROBE'] = '1'
if args.recovery_probe: env['BALANCE_RECOVERY_PROBE'] = '1'
if args.battery_clearance_probe:
    env['BALANCE_RECOVERY_PROBE'] = '1'
    env['BALANCE_BATTERY_CLEARANCE_ONLY'] = '1'
if args.path_workers > 0: env['DUNECITY_PATH_WORKERS'] = str(args.path_workers)
if args.air_defense_probe: env['BALANCE_AIR_DEFENSE_PROBE'] = '1'
if args.police_placement_probe: env['BALANCE_POLICE_PLACEMENT_PROBE'] = '1'
if args.police_budget_probe: env['BALANCE_POLICE_BUDGET_PROBE'] = '1'
if args.police_reinforcement_probe: env['BALANCE_POLICE_REINFORCEMENT_PROBE'] = '1'
if args.city_growth_probe: env['BALANCE_CITY_GROWTH_PROBE'] = '1'
if args.no_spice_city_probe: env['BALANCE_NO_SPICE_CITY_PROBE'] = '1'
if args.mcv_deployment_probe: env['BALANCE_MCV_DEPLOYMENT_PROBE'] = '1'
if args.custom_attack_probe: env['BALANCE_CUSTOM_ATTACK_PROBE'] = '1'
if args.whole_army_probe: env['BALANCE_WHOLE_ARMY_PROBE'] = '1'
if args.formation_measure: env['BALANCE_FORMATION_MEASURE'] = '1'
if args.wave_formation: env['BALANCE_WAVE_FORMATION'] = args.wave_formation
if args.wave_formation_probe: env['BALANCE_WAVE_FORMATION_PROBE'] = '1'
if args.blob_wave_probe: env['BALANCE_BLOB_WAVE_PROBE'] = '1'
if args.force_probe: env['BALANCE_FORCE_PROBE'] = '1'
# Play styles (label suffix @style, docs/ai-play-styles.md) are read from here by the probe.
env['BALANCE_STYLE_DIR'] = str(root / 'tests/ai/play-styles')
if args.match_sides:
    env['BALANCE_MATCH_SIDES'] = ','.join(f'{house}:{label}' for house, label in sides)
if args.skirmish_ffa_size:
    env['BALANCE_SKIRMISH_FFA_SIZE'] = str(args.skirmish_ffa_size)
    env['BALANCE_SKIRMISH_MAP_SEED'] = str(args.skirmish_map_seed)
if args.duel_map_size:
    env['BALANCE_DUEL_MAP_SIZE'] = str(args.duel_map_size)
    env['BALANCE_DUEL_MAP_SEED'] = str(args.duel_map_seed)
    try:
        env['BALANCE_DUEL_SIDES'] = ','.join(f'{house_names.index(house.strip().lower())}:{shape.strip()}'
                                             for house, shape in (side.split(':') for side in args.duel_sides.split(',')))
    except ValueError:
        parser.error('--duel-sides must be house:shape,house:shape with known houses.')
if args.launcher_kiting_probe: env['BALANCE_LAUNCHER_KITING_PROBE'] = '1'
if args.factory_recovery_probe: env['BALANCE_FACTORY_RECOVERY_PROBE'] = '1'
if args.strategic_site_probe: env['BALANCE_STRATEGIC_SITE_PROBE'] = '1'
if args.starport_probe: env['BALANCE_STARPORT_PROBE'] = '1'
if args.helper_economy_probe: env['BALANCE_HELPER_ECONOMY_PROBE'] = '1'
if args.city_campaign_probe: env['BALANCE_CITY_CAMPAIGN_PROBE'] = '1'
if args.no_population_victory_probe: env['BALANCE_NO_POPULATION_VICTORY_PROBE'] = '1'
if args.spice_income_probe: env['BALANCE_SPICE_INCOME_PROBE'] = '1'
if args.credit_storage_probe: env['BALANCE_CREDIT_STORAGE_PROBE'] = '1'
if args.stats_probe: env['BALANCE_STATS_PROBE'] = '1'
if args.pressure_probe: env['BALANCE_PRESSURE_PROBE'] = '1'
if args.defence_probe: env['BALANCE_DEFENCE_PROBE'] = '1'
if args.pacing_probe: env['BALANCE_PACING_PROBE'] = '1'
if args.repair_probe: env['BALANCE_REPAIR_PROBE'] = '1'
if args.scenario:
    env['BALANCE_SCENARIO'] = '1'
    if scenario_sides:
        env['BALANCE_SCENARIO_SIDES'] = ','.join(f'{house}:{team}:{player_class}'
                                                 for house, team, player_class in scenario_sides)
    if args.skirmish: env['BALANCE_SKIRMISH'] = args.skirmish
    if args.mission: env['BALANCE_MISSION'] = str(args.mission)
# Explicit either way: the game's own default ([General] AI Decision Log) is off.
env['DUNECITY_AI_TELEMETRY'] = '0' if args.no_ai_telemetry else '1'
with (out/'run.log').open('w') as log:
    subprocess.run([str(binary),'--window','--showlog'],cwd=out,env=env,
                   stdout=log,stderr=subprocess.STDOUT,check=True,timeout=args.wall_timeout)
results = [line for line in (out/'run.log').read_text(errors='replace').splitlines() if 'CAMPAIGN_BALANCE_RESULT:' in line]
if len(results) != 1: raise RuntimeError('Missing campaign result.')
events = None if args.no_ai_telemetry else next((out/'profile').rglob('events.jsonl'))
metadata, attacks, final = None, [], []
for line in events.open() if events else ():
    row = json.loads(line)
    if row['event'] == 'session_start': metadata = row['data']
    if row['event'] == 'ground_hunt': attacks.append(row)
    if row['event'] == 'game_summary': final.append(row)
    # The pressure fixture switches difficulty in-engine and checks each tier
    # itself; its Hard/Brutal waves must not inherit the CLI's default Easy cap.
    if not args.pressure_probe and row['event']=='ground_hunt' and row['data'].get('campaign_limited') and args.enemy_difficulty in ('easy','medium','hard'):
        d=row['data']
        # Caps apply to this dispatch. Surviving units from older waves are
        # deliberately allowed alongside it after the repeat timer expires.
        if (d['members']>d['alliance_unit_cap']
                or d['value']>d['alliance_value_cap']
                or d['alliance_units']>d['alliance_unit_cap']
                or d['alliance_value']>d['alliance_value_cap']
                or d['alliance_value']>d['attack_budget']):
            raise RuntimeError('Automatic campaign force exceeded its wave budget')
summary = {'result':results[0],'sourceCommit':source_commit,
           'workingTreeModified':source_modified,
           'metadata':metadata,'attacks':attacks,'final':final,'resolved_roster':roster,'enemy_controller':args.enemy_ai,'harvester_limit':args.harvester_limit}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(results[0])
print('Telemetry:',events or 'off (--no-ai-telemetry)')

if args.require_win and 'outcome=won' not in results[0]:
    raise RuntimeError('Full-match acceptance requires victory: ' + results[0])
