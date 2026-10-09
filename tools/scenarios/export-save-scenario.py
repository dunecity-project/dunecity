#!/usr/bin/env python3
"""Convert the current world of a saved DuneCity game into a playable scenario INI.

This is a diagnostic tool, not part of the shipped game. It injects
``tools/scenarios/save-scenario-export.inc`` into a generated ``main.cpp`` and
links it against the existing Ninja build, the same way the real-engine probes
under ``tests/`` do, so the conversion runs through the production save loader
and the production map-editor writer.

Three engine runs, one process each, so no run inherits another's state:

  fixture  (``--selftest`` only) build a small synthetic world and save it
  export   load the save, write the scenario INI and the captured facts
  verify   load the generated INI as an ordinary custom game, compare it against
           those facts, then step a bounded match

Everything happens in an isolated ``DUNECITY_USERDIR`` with a dead metaserver
address, dummy SDL drivers and the intro disabled. The input save is only read.

Examples::

    tools/scenarios/export-save-scenario.py \\
        --save  .../capture/save/3waysplit.dls \\
        --output .../export-work/3waysplit-current.ini \\
        --work-dir .../export-work/run

    tools/scenarios/export-save-scenario.py --selftest --work-dir /tmp/selftest
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import prepare_profile  # noqa: E402

TARGET = 'bin/dunecity.app/Contents/MacOS/dunecity'
NEEDLE = 'int menuResult = MainMenu().showMenu();'
DEFAULT_FIXTURE_MAP = root / 'data/maps/singleplayer/3P - 64x32 - Middle Man.ini'

# What an ordinary scenario INI cannot carry, and therefore what a converted
# world starts without. Recorded in the report so nobody mistakes the result for
# a save-game replay.
KNOWN_RESETS = [
    'city simulation state (population, funds, demand, tax receipts)',
    'per-zone residential population and tile zone density',
    'production queues, build progress and unit orders/targets',
    'structure upgrade levels',
    'unit cargo and the contents of carryalls, refineries and repair yards',
    'fog of war and per-house exploration',
    'game cycle count and elapsed match time',
    'lobby game options (concrete required, fog, spice income factors, unit limits)',
    'fractional per-tile spice amounts (terrain class is kept, the amount is re-rolled)',
    'destroyed-structure rubble overlays',
    'CHOAM stock, which is reset to the map editor defaults',
]


def reject_aliased_paths(save, output, report):
    """Refuse to run when two of the three paths name the same file.

    ``Path.resolve`` already collapses ordinary symlink aliases, but it keeps a
    hardlink's own name and, on a case-insensitive filesystem, whatever case the
    caller typed. Either alias would let the scenario or the report overwrite the
    input save. Where both paths exist, ``samefile`` settles it on filesystem
    identity instead of on the text; comparing the text still covers two
    destinations that do not exist yet. This runs before ``--force`` and before
    anything is written.
    """
    labelled = (('save input', save), ('scenario output', output), ('report', report))
    for index, (first, left) in enumerate(labelled):
        for second, right in labelled[index + 1:]:
            same = left == right
            if not same and left.exists() and right.exists():
                try:
                    same = left.samefile(right)
                except OSError:
                    same = False
            if same:
                raise SystemExit('The ' + first + ' and the ' + second + ' must be distinct paths: '
                                 + str(left) + ' and ' + str(right) + ' are the same file.')


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, default=root / 'build')
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='Scratch directory for the generated binary, profile and logs')
    parser.add_argument('--save', type=Path,
                        help='Saved game to convert (required unless --selftest)')
    parser.add_argument('--output', type=Path,
                        help='Scenario INI to write (defaults to <work-dir>/<save stem>-current.ini)')
    parser.add_argument('--report', type=Path,
                        help='Machine-readable report (defaults to <work-dir>/report.json)')
    parser.add_argument('--force', action='store_true',
                        help='Overwrite an existing --output; refused by default')
    parser.add_argument('--match-seconds', type=int, default=60,
                        help='In-game seconds to step after loading the generated scenario')
    parser.add_argument('--search-radius', type=int, default=24,
                        help='How far a unit may be relocated when its own tile is unusable')
    parser.add_argument('--author', default='DuneCity save export',
                        help='BASIC.Author for the generated scenario')
    parser.add_argument('--profile-from', type=Path,
                        help='Copy settings and installed mods from this profile so the exact '
                             'pinned mod revision of the save resolves offline')
    parser.add_argument('--selftest', action='store_true',
                        help='Build a synthetic world first and convert that instead of --save')
    parser.add_argument('--fixture-map', type=Path, default=DEFAULT_FIXTURE_MAP,
                        help='Bundled map the synthetic world is built on')
    parser.add_argument('--min-structures', type=int, default=0,
                        help='Fail unless the reloaded scenario holds at least this many structures. '
                             'Use it to prove a converted world is not merely a starting map.')
    parser.add_argument('--min-units', type=int, default=0,
                        help='Fail unless the reloaded scenario holds at least this many units')
    parser.add_argument('--keep-going', action='store_true',
                        help='Write the report even when a phase fails')
    arguments = parser.parse_args()
    if not arguments.selftest and arguments.save is None:
        parser.error('--save is required unless --selftest is given.')
    if not 1 <= arguments.match_seconds <= 3600:
        parser.error('--match-seconds must be between 1 and 3600.')
    if not 1 <= arguments.search_radius <= 64:
        parser.error('--search-radius must be between 1 and 64.')
    return arguments


def build_probe(build, work):
    """Compile and link the diagnostic binary from the existing Ninja build."""
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True)
    commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', TARGET],
                                       text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    if main.count(NEEDLE) != 1:
        raise RuntimeError('Main-menu injection point changed; update export-save-scenario.py.')
    main = main.replace(NEEDLE, 'int menuResult = runSaveScenarioExport();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))',
                        'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    injection = ('#define DUNECITY_SCENARIO_FIXTURE_INC "'
                 + str(root / 'tests/scenarios/save-scenario-fixture.inc') + '"\n'
                 + '#include "' + str(root / 'tools/scenarios/save-scenario-export.inc') + '"\n')
    main = main[:position] + injection + main[position:]
    source = work / 'scenario-export-main.cpp'
    source.write_text(main)

    compile_command = shlex.split(next(line for line in commands
                                       if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            index = compile_command.index(option)
            del compile_command[index:index + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    obj = work / 'scenario-export-main.o'
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    # Diagnostic-only: lets the tool read the loaded world and the editor's own
    # fields without adding accessors to shipped classes.
    compile_command.append('-fno-access-control')

    app = work / 'scenario-export.app/Contents'
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    resources = app / 'Resources'
    if not resources.exists():
        resources.symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/scenario-export'
    link = shlex.split(next(line for line in commands if ' -o ' + TARGET + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if argument.endswith('/main.cpp.o') else argument for argument in link]

    with (work / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def run_phase(binary, work, profile, name, extra_env, timeout=1800):
    """One engine process. Returns the SCENARIO_EXPORT_ lines it logged."""
    environment = dict(os.environ,
                       DUNECITY_USERDIR=str(profile),
                       SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy',
                       SCENARIO_EXPORT_MODE=name,
                       **extra_env)
    log_path = work / ('run-' + name + '.log')
    with log_path.open('w') as log:
        subprocess.run([str(binary), '--window', '--showlog'], cwd=work, env=environment,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=timeout)
    lines = [line for line in log_path.read_text().splitlines() if 'SCENARIO_EXPORT_' in line]
    for line in lines:
        print(line)
    if not any(('SCENARIO_EXPORT_PASS: ' + name) in line for line in lines):
        raise RuntimeError('Phase ' + name + ' did not pass; see ' + str(log_path))
    return lines


def read_facts(path):
    """The flat facts the engine wrote, grouped by record kind."""
    facts = {}
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        kind, _, rest = line.partition(' ')
        facts.setdefault(kind, []).append(rest)
    return facts


def scalar(facts, kind, convert=int, default=None):
    values = facts.get(kind)
    if not values:
        return default
    return convert(values[0].split()[0]) if convert is int else convert(values[0])


def summarise_export(facts):
    houses, structures_by_house, units_by_house = {}, {}, {}
    for row in facts.get('house', []):
        fields = row.split()
        house_id, team, credits, structures, units, active = (int(fields[i]) for i in range(6))
        houses[fields[6] if len(fields) > 6 else str(house_id)] = {
            'house_id': house_id, 'team': team, 'credits': credits,
            'structures': structures, 'units': units, 'active': bool(active),
        }
    for row in facts.get('struct', []):
        house = int(row.split()[0])
        structures_by_house[house] = structures_by_house.get(house, 0) + 1
    for row in facts.get('unit', []):
        house = int(row.split()[0])
        units_by_house[house] = units_by_house.get(house, 0) + 1
    relocated = []
    for row in facts.get('unit', []):
        fields = row.split()
        if int(fields[7]):
            relocated.append({
                'item_id': int(fields[1]), 'object_id': int(fields[11]),
                'house': int(fields[0]),
                'source': [int(fields[8]), int(fields[9])],
                'placed': [int(fields[2]), int(fields[3])],
                'on_map_in_save': bool(int(fields[10])),
            })
    items = {}
    for row in facts.get('item', []):
        fields = row.split(' ', 2)
        items[fields[2]] = int(fields[1])
    size = facts.get('size', ['0 0'])[0].split()
    return {
        'dimensions': {'x': int(size[0]), 'y': int(size[1])},
        'tech_level': scalar(facts, 'tech', default=0),
        'win_flags': scalar(facts, 'win', default=0),
        'lose_flags': scalar(facts, 'lose', default=0),
        'source_game_cycle': scalar(facts, 'cycle', default=0),
        'source_mod': scalar(facts, 'mod', convert=str, default=''),
        'city_simulation_active_in_save': bool(scalar(facts, 'citysim', default=0)),
        'substrate_fallback_house': scalar(facts, 'substrate_fallback_house', default=0),
        'structures_total': len(facts.get('struct', [])),
        'units_total': len(facts.get('unit', [])),
        'concrete_tiles': len(facts.get('concrete', [])),
        'road_tiles': len(facts.get('road', [])),
        'houses': houses,
        'structures_per_house': structures_by_house,
        'units_per_house': units_by_house,
        'relocated_units': relocated,
        'reset_order_modes': [dict(zip(('object_id', 'source_mode', 'scenario_mode'),
                                      map(int, row.split())))
                              for row in facts.get('reset_mode', [])],
        'unowned_substrate_tiles': sum(int(row.split()[2]) < 0
                                      for kind in ('road', 'concrete')
                                      for row in facts.get(kind, [])),
        'item_counts': items,
    }


def summarise_verify(facts):
    numbers = {}
    for kind in ('structures', 'units', 'concrete', 'roads', 'active_houses', 'terrain_mismatch',
                 'road_missing', 'added_auto_roads', 'unexpected_roads', 'substrate_owner_mismatch', 'structure_missing', 'unit_missing',
                 'unit_angle_mismatch', 'unit_mode_mismatch', 'health_outside_quantization',
                 'relocated_units', 'match_seconds', 'match_cycles', 'match_finished_early',
                 'failures'):
        value = scalar(facts, kind)
        if value is not None:
            numbers[kind] = value
    houses = {}
    for row in facts.get('house', []):
        fields = [int(field) for field in row.split()]
        houses[fields[0]] = {'team': fields[1], 'credits': fields[2],
                             'structures': fields[3], 'units': fields[4]}
    items = {}
    for row in facts.get('item', []):
        fields = row.split(' ', 2)
        items[fields[2]] = int(fields[1])
    return {'counts': numbers, 'houses': houses, 'item_counts': items,
            'automatic_road_frontage': [list(map(int, row.split())) for row in facts.get('auto_road', [])],
            'failures': facts.get('failure', [])}


def main():
    arguments = parse_arguments()
    build = arguments.build_dir.resolve()
    work = arguments.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)

    save = (work / 'fixture-world.dls') if arguments.selftest else arguments.save.resolve()
    output = (arguments.output.resolve() if arguments.output
              else work / (save.stem + '-current.ini'))
    report_path = arguments.report.resolve() if arguments.report else work / 'report.json'
    reject_aliased_paths(save, output, report_path)
    if output.exists() and not arguments.force:
        raise SystemExit('Refusing to overwrite ' + str(output) + '; pass --force or pick another path.')
    if not arguments.selftest and not save.is_file():
        raise SystemExit('No such saved game: ' + str(save))

    facts_path = work / 'export-facts.txt'
    verify_path = work / 'verify-facts.txt'
    for stale in (facts_path, verify_path):
        if stale.exists():
            stale.unlink()

    binary = build_probe(build, work)
    profile = work / 'profile'
    active_mod = prepare_profile(profile, arguments.profile_from)
    if active_mod:
        print('SCENARIO_EXPORT_PROFILE: active_mod=' + active_mod)

    report = {
        'tool': 'tools/scenarios/export-save-scenario.py',
        'mode': 'selftest' if arguments.selftest else 'save',
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root,
                                                 text=True).strip(),
        'working_tree_modified': bool(subprocess.check_output(
            ['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
        'tool_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'probe_source_sha256': hashlib.sha256(
            (root / 'tools/scenarios/save-scenario-export.inc').read_bytes()).hexdigest(),
        'inputs': {
            'save': str(save),
            'fixture_map': str(arguments.fixture_map) if arguments.selftest else None,
            'search_radius': arguments.search_radius,
            'match_seconds': arguments.match_seconds,
        },
        'outputs': {'scenario': str(output), 'work_dir': str(work)},
        'known_resets': KNOWN_RESETS,
        'phases': {},
    }

    try:
        if arguments.selftest:
            fixture_map = arguments.fixture_map.resolve()
            if not fixture_map.is_file():
                raise SystemExit('No such fixture map: ' + str(fixture_map))
            run_phase(binary, work, profile, 'fixture', {
                'SCENARIO_EXPORT_SAVE': str(save),
                'SCENARIO_EXPORT_FIXTURE_MAP': str(fixture_map),
            })
            report['inputs']['fixture_map_sha256'] = hashlib.sha256(
                fixture_map.read_bytes()).hexdigest()

        save_bytes = save.read_bytes()
        report['inputs']['save_sha256'] = hashlib.sha256(save_bytes).hexdigest()
        report['inputs']['save_bytes'] = len(save_bytes)

        run_phase(binary, work, profile, 'export', {
            'SCENARIO_EXPORT_SAVE': str(save),
            'SCENARIO_EXPORT_OUTPUT': str(output),
            'SCENARIO_EXPORT_FACTS': str(facts_path),
            'SCENARIO_EXPORT_AUTHOR': arguments.author,
            'SCENARIO_EXPORT_SEARCH_RADIUS': str(arguments.search_radius),
        })
        # The input save must come out byte-identical: this tool only reads it.
        if hashlib.sha256(save.read_bytes()).hexdigest() != report['inputs']['save_sha256']:
            raise RuntimeError('The exporter modified its input save.')
        report['phases']['export'] = summarise_export(read_facts(facts_path))
        report['outputs']['scenario_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
        report['outputs']['scenario_bytes'] = output.stat().st_size
        report['outputs']['revision_sidecar'] = (
            str(output) + '.workshop.ini' if Path(str(output) + '.workshop.ini').exists() else None)

        run_phase(binary, work, profile, 'verify', {
            'SCENARIO_EXPORT_OUTPUT': str(output),
            'SCENARIO_EXPORT_FACTS': str(facts_path),
            'SCENARIO_EXPORT_VERIFY_FACTS': str(verify_path),
            'SCENARIO_EXPORT_MATCH_SECONDS': str(arguments.match_seconds),
        })
        report['phases']['verify'] = summarise_verify(read_facts(verify_path))
        counts = report['phases']['verify']['counts']
        report['thresholds'] = {'min_structures': arguments.min_structures,
                                'min_units': arguments.min_units}
        if counts.get('structures', 0) < arguments.min_structures:
            raise RuntimeError('The reloaded scenario holds %d structures, fewer than the required %d.'
                               % (counts.get('structures', 0), arguments.min_structures))
        if counts.get('units', 0) < arguments.min_units:
            raise RuntimeError('The reloaded scenario holds %d units, fewer than the required %d.'
                               % (counts.get('units', 0), arguments.min_units))
        report['result'] = 'pass'
    except Exception as error:                      # noqa: BLE001 - reported, then re-raised
        report['result'] = 'fail'
        report['error'] = str(error)
        if verify_path.exists():
            report['phases']['verify'] = summarise_verify(read_facts(verify_path))
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
        print('SCENARIO_EXPORT_REPORT: ' + str(report_path))
        if not arguments.keep_going:
            raise
        return 1

    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print('SCENARIO_EXPORT_REPORT: ' + str(report_path))
    print('SCENARIO_EXPORT_SCENARIO: ' + str(output))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
