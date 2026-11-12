#!/usr/bin/env python3
"""Preregister the fixed economic-fleet 3 x 3 x 2 factorial; never run games.

Generate the 18 numeric-level styles, explicit OFF, common cases and literal plan:
  python3 tests/ai/econ-ratios-design.py --source-root .
After the parent gate, emit a submission-ready output-only plan with the real pin:
  python3 tests/ai/econ-ratios-design.py --source-root . --pin FULL_SHA
Fresh-seed full-game funnel (native is always added; styles are selected by a human):
  python3 tests/ai/econ-ratios-design.py --stage full-games-48 --seed-start 1180200 \
      --minutes 90 --cases-count 24 --styles econ-ratio-h2-cq1-r0 \
      econ-ratio-h3-cq1-r1 --pin FULL_SHA

Outputs: tests/ai/play-styles/econ-ratio-*.ini (19 files), schema-v1 case
manifests at tests/ai/screen-designs/econ-ratios-STAGE-cases.json, and
OUTPUT/STAGE/{cases.json,design.json,commands.sh,submissions.sh}. Existing
identical assets are not rewritten; different existing evidence is never replaced.
Source case manifests deliberately do not embed their own future commit SHA.
Without --pin, source_sha is null and submission argv contain <PIN_AFTER_GATE>;
those plans are explicitly NOT submission-ready. --pin changes output plans only.
The initial plan also contains additional_blocks for observed handmade controls:
four maps x three fresh seeds x (18 ratios + native) in both mods; the four
already-known refinery6 failure pairs for 15-minute mechanism snapshots; and
both Silicon Valley control seeds for new-ratio parity only. These are separate
diagnostics, never substitutes for the fixed unobserved control promotion gate.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import re
import shlex

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path('/Users/stefan/Documents/projects/outputs/ai-tuning-20261108/EconRatios')
BASE = '97b6698c'
SEED_MIN, SEED_MAX = 1180000, 1189999
SIZES, TECHS = (64, 128, 160), (3, 6, 9)
HARVESTERS, CARRYALLS, REPAIRS = (2, 3, 4), (0, 1, 2), (0, 1)
SECTION = 'Force Concentration'
OFF_STYLE = 'econ-ratio-off'
NATIVE = 'native'


def screen_tools(source_root):
    spec = importlib.util.spec_from_file_location(
        'econ_ratios_screen_design', source_root / 'tests/ai/ai-screen-design.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def style_name(harvesters, carryall_quarters, repair_yards):
    return f'econ-ratio-h{harvesters}-cq{carryall_quarters}-r{repair_yards}'


def style_rows():
    return [dict(style=style_name(h, c, r), enabled=True, harvesters_per_bay=h,
                 carryall_quarters=c, repair_yards=r)
            for h, c, r in itertools.product(HARVESTERS, CARRYALLS, REPAIRS)]


def ini_text(row):
    return ('# Fixed preregistered economic-fleet ratio; no numeric search.\n'
            f'[{SECTION}]\n'
            f'EconomicFleetRatios={int(row["enabled"])}\n'
            f'FleetHarvestersPerBay={row["harvesters_per_bay"]}\n'
            f'FleetCarryallQuarters={row["carryall_quarters"]}\n'
            f'FleetRepairYards={row["repair_yards"]}\n')


def json_text(value):
    return json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n'


def preserve_write(path, text):
    """Generation is additive: never silently overwrite an earlier preregistration."""
    if path.exists():
        if path.read_text() != text:
            raise ValueError(f'Refusing to replace different existing evidence: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def common_cases(tools, seed_start, count):
    """Use StatsDesign's grid, not its BOOL factorial or its random game seeds.

    Select one spice cell per format/size in each 12-case Latin block. Replacing
    the helper's seeds with the reserved literal integers is intentional. Tech
    and terrain each take all three levels within every format and block.
    Across successive blocks the tested joint cells rotate, not arm assignments.
    """
    blocks = count // 12
    grid = tools.cases(seed_start, list(tools.FORMATS), list(SIZES),
                       list(tools.SPICES), list(TECHS), replicate=blocks)
    indexed = {(cell['replicate'], cell['format'], cell['size'], cell['spice_level']): cell
               for cell in grid['cases']}
    rows = []
    for block, fi, si in itertools.product(range(blocks), range(4), range(3)):
        fmt = tools.FORMATS[fi]
        spice = tools.SPICES[(si + fi + block) % 3]
        cell = dict(indexed[block, fmt, SIZES[si], spice])
        # Grid selection alone aliases terrain to a constant, so Latin-balance
        # terrain explicitly while retaining StatsDesign's valid compositions.
        composition = tools.COMPOSITIONS[(si + 2 * fi + block) % 3]
        cell.update(seed=seed_start + len(rows), difficulty='brutal',
                    composition=composition['name'], rock=composition['rock'],
                    mountain=composition['mountain'], replicate=block)
        rows.append(cell)
    return tools.validate_cases(dict(schema_version=1, sample_seed=seed_start, cases=rows))


def joint_coverage(tools, rows):
    result = tools.coverage(rows)
    result['joint_format_size_spice_tech'] = [dict(
        format=fmt, size=size, spice_level=spice, initial_tech=tech,
        cases=sum(cell['format'] == fmt and cell['size'] == size
                  and cell['spice_level'] == spice and cell['initial_tech'] == tech
                  for cell in rows))
        for fmt, size, spice, tech in itertools.product(tools.FORMATS, SIZES,
                                                       tools.SPICES, TECHS)]
    result['joint_note'] = ('All 108 format x size x spice x tech cells are listed, including zeros. '
                            'Latin balancing is not a full joint map-feature crossing.')
    return result


def command(mod, case_path, result_dir, minutes, style):
    argv = ['python3', 'tests/ai/dunecity-audit.py', '--mode', 'sample', '--mod', mod,
            '--case-manifest', case_path.as_posix(), '--minutes', str(minutes),
            '--base', 'cy', '--army', '0', '--city', '0', '--jobs', '1',
            '--export-maps', 'maps', '--results', str(result_dir)]
    if style != NATIVE:
        argv += ['--candidate-style', style]
    return argv


def replace_pin_token(value, pin):
    if isinstance(value, dict):
        return {key: replace_pin_token(item, pin) for key, item in value.items()}
    if isinstance(value, list):
        return [replace_pin_token(item, pin) for item in value]
    return pin if value == '<PIN_AFTER_GATE>' else value


def handmade_blocks(source_root, root, assets, pin):
    """Authored control schedules, distinct from generated map/seat inference.

    These use the parent's diagnostic controls extension, not the unobserved
    fixed control gate. --style is candidate-only ONLY under --tier controls.
    """
    maps = (
        ('05-mountain-pass', '4P - 128x128 - Mountain Pass', 'ffa4', 128, 10801),
        ('06-wormhole', '4P - 128x128 - Wormhole', 'ffa4', 128, 11810),
        ('09-fertile-basin', '6P - 64x64 - Fertile Basin', 'ffa6', 64, 10801),
        ('10-enclaves', '6P - 128x128 - Enclaves', 'ffa6', 128, 11810))
    silicon = ('13-silicon-valley', '4P - 64x64 - Silicon Valley', 'ffa4', 64, None)
    native = dict(style=NATIVE, enabled=False, ini=None, levels_not_applicable=True)
    on = [dict(row) for row in assets if row['enabled']]
    configurations = (
        ('handmade-throughput', maps, (1180100, 1180101, 1180102), on + [native], ('vanilla', 'dunecity'), False),
        ('refinery6-mechanism', maps, None,
         [dict(style='vanilla-boom-refinery-6', enabled=False, levels_not_applicable=True), native],
         ('vanilla',), True),
        ('silicon-valley-ratio-parity', (silicon,), (10801, 11810),
         [dict(row) for row in assets] + [native], ('vanilla', 'dunecity'), False))
    blocks = []
    for stage, selected_maps, seeds, arms, mods, exact_pairs in configurations:
        cells = []
        for cell, name, fmt, size, failure_seed in selected_maps:
            for seed in ((failure_seed,) if exact_pairs else seeds):
                cells.append(dict(cell=cell, map=name, path=f'data/maps/multiplayer/{name}.ini',
                                  format=fmt, size=size, seed=seed, spice_level='authored',
                                  initial_tech=None, composition='authored'))
        source_case = Path('tests/ai/screen-designs') / f'econ-ratios-{stage}-cases.json'
        manifest = dict(schema_version=1, kind='authored-control-diagnostic-cases', cases=cells,
                        note='Schedule specification, NOT a generated-map --case-manifest. The scenario harness consumes --cells/--cases and diagnostic control flags.')
        preserve_write(source_root / source_case, json_text(manifest))
        preserve_write(root / stage / 'cases.json', json_text(manifest))
        pattern = '^controls/(' + '|'.join(row[0] for row in selected_maps) + ')/'
        pair_pattern = '^controls/(?:' + '|'.join(
            f'{cell}/vanilla/{failure_seed}' for cell, _, _, _, failure_seed in selected_maps) + ')$' if exact_pairs else None
        commands = []
        for mod, arm in itertools.product(mods, arms):
            result = root / stage / 'runs' / mod / arm['style']
            argv = ['python3', 'tests/ai/scenario-matrix.py', '--tier', 'controls',
                    '--control-diagnostics', '--economy-samples',
                    '--control-mod', mod, '--minutes', '15', '--jobs', '1',
                    '--cells', pattern, '--results', str(result)]
            if exact_pairs:
                argv += ['--attack-events', '--keep-telemetry']
            if exact_pairs:
                argv += ['--cases', pair_pattern]
            elif stage == 'handmade-throughput':
                argv += ['--control-seeds'] + [str(seed) for seed in seeds]
            if arm['style'] != NATIVE:
                argv += ['--style', arm['style']]
            commands.append(dict(mod=mod, style=arm['style'], result_dir=str(result), argv=argv,
                                 games=len(cells), queue_argv=[
                                     'dunecity-queue', 'submit', '--cwd', '.', '--rev',
                                     pin or '<PIN_AFTER_GATE>', '--'] + argv))
        smoke = []
        if stage == 'handmade-throughput':
            for mod in mods:
                result = root / stage / 'startup-smoke' / mod
                argv = ['python3', 'tests/ai/scenario-matrix.py', '--tier', 'controls',
                        '--control-diagnostics', '--economy-samples',
                        '--control-mod', mod, '--control-seeds', '1180100', '--minutes', '2', '--jobs', '1',
                        '--cells', '^controls/05-mountain-pass/', '--style', style_name(3, 1, 1),
                        '--results', str(result)]
                smoke.append(dict(mod=mod, argv=argv, games=1, queue_argv=[
                    'dunecity-queue', 'submit', '--cwd', '.', '--rev', pin or '<PIN_AFTER_GATE>', '--'] + argv))
        block = dict(schema_version=1, kind='authored-economic-ratio-diagnostic', stage=stage,
                     case_mode='authored', cases=cells, arms=arms, commands=commands,
                     sample_seed=1180100 if stage == 'handmade-throughput' else 0,
                     case_manifest=source_case.as_posix(),
                     case_manifest_sha256=hashlib.sha256(json_text(manifest).encode()).hexdigest(),
                     protocol=dict(minutes=15, mods=list(mods), observer_mode='on', candidate_only=True,
                                   opponent_config='native', candidate_seats=['candidate'],
                                   checkpoints=[3, 5, 8, 10, 15], diagnostic_only=True),
                     smoke=dict(commands=smoke, required_before_mass_submission=bool(smoke)),
                     game_counts=dict(games_per_arm_per_mod=len(cells), arms_per_mod=len(arms),
                                      total=sum(item['games'] for item in commands)),
                     inference_note=('Exact already-known failure pairs, shortened to diagnose mechanisms; NOT reconfirmation of full-game failure.'
                                     if exact_pairs else
                                     'Authored starting positions/rosters are preserved. No generated-map spice/tech labels or two-seat balance is claimed.'),
                     required_receipt='control_diagnostics=1; all arms observed identically; never a fixed-control promotion gate',
                     seed_exception='10801/11810 are required external controls' if stage != 'handmade-throughput' else None)
        preserve_write(root / stage / 'commands.sh', '\n'.join(shlex.join(item['argv']) for item in commands) + '\n')
        blocks.append(block)
    return blocks


def generate(args):
    source_root = args.source_root.resolve()
    tools = screen_tools(source_root)
    root = args.output.resolve() / args.stage
    manifest = common_cases(tools, args.seed_start, args.cases_count)
    source_case = Path('tests/ai/screen-designs') / f'econ-ratios-{args.stage}-cases.json'
    preserve_write(source_root / source_case, json_text(manifest))
    preserve_write(root / 'cases.json', json_text(manifest))
    factorial = style_rows()
    off = dict(style=OFF_STYLE, enabled=False, harvesters_per_bay=2,
               carryall_quarters=0, repair_yards=0)
    assets = factorial + [off]
    for row in assets:
        relative = Path('tests/ai/play-styles') / (row['style'] + '.ini')
        text = ini_text(row)
        preserve_write(source_root / relative, text)
        row.update(ini=relative.as_posix(), ini_sha256=hashlib.sha256(text.encode()).hexdigest())
    selected = args.styles or [row['style'] for row in assets]
    by_style = {row['style']: row for row in assets}
    native = dict(style=NATIVE, enabled=False, ini=None, levels_not_applicable=True)
    arms = [dict(by_style[name]) for name in selected] + [native]
    pin = args.pin.lower() if args.pin else None
    commands = []
    for mod, arm in itertools.product(('vanilla', 'dunecity'), arms):
        result = root / 'runs' / mod / arm['style']
        argv = command(mod, source_case, result, args.minutes, arm['style'])
        commands.append(dict(mod=mod, style=arm['style'], result_dir=str(result), argv=argv,
                             games=2 * args.cases_count,
                             queue_argv=['dunecity-queue', 'submit', '--cwd', '.', '--rev',
                                         pin or '<PIN_AFTER_GATE>', '--'] + argv))
    smoke_case = Path('tests/ai/screen-designs/econ-ratios-startup-smoke-cases.json')
    smoke_manifest = tools.validate_cases(dict(schema_version=1, sample_seed=1180099, cases=[
        dict(format='1v1', size=64, spice_level='low', initial_tech=3, difficulty='brutal',
             seed=1180099, rock=15, mountain=5, composition='scarce-rock', replicate=0)]))
    preserve_write(source_root / smoke_case, json_text(smoke_manifest))
    preserve_write(root / 'smoke-cases.json', json_text(smoke_manifest))
    smoke_commands = []
    for mod in ('vanilla', 'dunecity'):
        result = root / 'startup-smoke' / mod
        argv = command(mod, smoke_case, result, 2, style_name(3, 1, 1))
        smoke_commands.append(dict(mod=mod, style=style_name(3, 1, 1), result_dir=str(result),
                                   argv=argv, games=2, queue_argv=[
                                       'dunecity-queue', 'submit', '--cwd', '.', '--rev',
                                       pin or '<PIN_AFTER_GATE>', '--'] + argv))
    plan = dict(
        schema_version=1, kind='economic-fleet-full-categorical-factorial', stage=args.stage,
        declared_base=BASE, source_sha=pin, submission_ready=bool(pin), sample_seed=args.seed_start,
        case_manifest=source_case.as_posix(), case_manifest_output=str(root / 'cases.json'),
        case_manifest_sha256=hashlib.sha256(json_text(manifest).encode()).hexdigest(),
        cases=manifest['cases'], factors=[
            dict(name='harvesters_per_bay', section=SECTION, key='FleetHarvestersPerBay', levels=list(HARVESTERS)),
            dict(name='carryall_quarters', section=SECTION, key='FleetCarryallQuarters', levels=list(CARRYALLS),
                 unit='quarter carryalls per harvester; 0, 0.25, 0.5 carryalls/harvester'),
            dict(name='repair_yards', section=SECTION, key='FleetRepairYards', levels=list(REPAIRS))],
        master=dict(section=SECTION, key='EconomicFleetRatios', default=0, on=1, off=0),
        complete_factorial=style_rows(), generated_style_count=19, arms=arms, commands=commands,
        additional_blocks=handmade_blocks(source_root, root, assets, pin) if args.stage == 'screen-throughput' else [],
        smoke=dict(case_manifest=smoke_case.as_posix(), cases=smoke_manifest['cases'],
                   minutes=2, commands=smoke_commands, required_before_mass_submission=True,
                   gate='After the pinned unit gate, run both real Linux harness startup commands through the same queue argv transport. Unit PASS alone does not prove the scenario probe compiles or starts.'),
        protocol=dict(mods=['vanilla', 'dunecity'], minutes=args.minutes, base='cy', army=0, city=0,
                      candidate_tier='brutal', opponent_tier='brutal', opponent_config='native',
                      candidate_only=True, observer_mode='on',
                      forbidden_environment=['DUNECITY_TECH_SAVINGS_AUDIT'],
                      candidate_seats=['original', 'swapped'],
                      seat_note='Harness swaps seats 0/last, or seats 0/2 for 2v2; it also rotates house order by case.',
                      campaign_scope='Not a campaign schedule; campaign idle-human + Brutal-support checks are separate.'),
        coverage=joint_coverage(tools, manifest['cases']),
        game_counts=dict(cases_per_arm_per_mod=args.cases_count, games_per_arm_per_mod=2 * args.cases_count,
                         arms_per_mod=len(arms), per_mod=len(arms) * 2 * args.cases_count,
                         total=len(commands) * 2 * args.cases_count),
        inference=dict(factorial='18 fixed categorical treatments; all two- and three-factor ratio interactions are unaliased by construction.',
                       controls='Native shared arm and explicit OFF are not factorial cells; no boolean encoding of numeric levels.',
                       map_cells='Common cases, literal seeds and both physical candidate seats are unchanged across every arm and mod.',
                       clustering='Resample common sample_seed/sample_case blocks, never independent houses or minute samples.',
                       throughput='Observed game exposure, including early endings; no low-income, defeated or eliminated candidate filter.',
                       capital='Current replacement-price asset credit is a stock, NOT actual cumulative capital spending.',
                       confirmation='Screen is diagnostic. Human-selected styles proceed on fresh seeds; no automatic numeric parameter search.',
                       execution='Run argv from the pinned source root. Plan generation never executes the harness or submits jobs.'))
    # --pin is an explicit parent-authorized output-only finalization. Earlier
    # unpinned plans are retained alongside the finalized plan, not destroyed.
    design_path = root / 'design.json'
    if design_path.exists() and design_path.read_text() != json_text(plan) and pin:
        old = json.loads(design_path.read_text())
        if old.get('source_sha') is not None:
            raise ValueError(f'Already pinned plan differs: {design_path}; choose a new stage/output')
        expected = replace_pin_token(old, pin)
        expected.update(source_sha=pin, submission_ready=True)
        if expected != plan:
            raise ValueError('Pin finalization may not alter preregistered cells, styles or commands')
        preserve_write(root / 'design-unpinned.json', json_text(old))
        design_path.write_text(json_text(plan))
        preserve_write(root / 'submissions-unpinned.sh', '\n'.join(
            shlex.join(item['queue_argv']) for item in old['commands']) + '\n')
        (root / 'submissions.sh').write_text('\n'.join(shlex.join(item['queue_argv']) for item in commands) + '\n')
    else:
        preserve_write(design_path, json_text(plan))
        preserve_write(root / 'submissions.sh', '\n'.join(shlex.join(item['queue_argv']) for item in commands) + '\n')
    preserve_write(root / 'commands.sh', '\n'.join(shlex.join(item['argv']) for item in commands) + '\n')
    preserve_write(root / 'smoke-commands.sh', '\n'.join(
        shlex.join(item['argv']) for item in smoke_commands) + '\n')
    print(json.dumps(dict(design=str(design_path), submission_ready=bool(pin), **plan['game_counts']), indent=2))
    return plan


def parser():
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument('--source-root', type=Path, default=ROOT)
    result.add_argument('--output', type=Path, default=OUTPUT, help='Absolute evidence root; STAGE is appended')
    result.add_argument('--stage', default='screen-throughput', help='Stable preregistered stage label')
    result.add_argument('--pin', help='Parent gate full 40-hex SHA; never infer HEAD or pin the declared base')
    result.add_argument('--seed-start', type=int, help='Literal first seed, reserved lane 1180000-1189999')
    result.add_argument('--minutes', type=int, help='Screen fixed at 15; full games require 16-90')
    result.add_argument('--cases-count', type=int, choices=(12, 24, 48), help='12/24/48 cases = 24/48/96 games per arm per mod')
    result.add_argument('--styles', nargs='+', choices=[row['style'] for row in style_rows()] + [OFF_STYLE],
                        help='Parent-selected fixed style names for a fresh-seed stage; native is always included')
    return result


def main():
    cli = parser()
    args = cli.parse_args()
    if not re.fullmatch(r'[a-z][a-z0-9-]*', args.stage):
        cli.error('--stage must be a lowercase filesystem-safe label')
    if args.pin and not re.fullmatch(r'[0-9a-fA-F]{40}', args.pin):
        cli.error('--pin must be the full immutable 40-hex parent gate SHA')
    if args.stage == 'screen-throughput':
        if (args.seed_start not in (None, SEED_MIN) or args.minutes not in (None, 15)
                or args.cases_count not in (None, 12) or args.styles is not None):
            cli.error('screen-throughput is fixed: all 18 styles + OFF + native, seeds 1180000-1180011, 12 cases, 15 minutes')
        args.seed_start, args.minutes, args.cases_count = SEED_MIN, 15, 12
    elif (args.seed_start is None or args.minutes is None or args.cases_count is None or not args.styles):
        cli.error('Fresh-seed stages require --seed-start, --minutes, --cases-count and --styles')
    elif not 16 <= args.minutes <= 90 or args.seed_start < SEED_MIN + 12:
        cli.error('Full games use 16-90 minutes and fresh seeds after the 12 reserved screen seeds')
    if args.stage != 'screen-throughput' and args.seed_start <= 1180102 and args.seed_start + args.cases_count - 1 >= 1180099:
        cli.error('Seeds 1180099-1180102 are reserved for startup smoke and handmade diagnostics')
    if args.seed_start < SEED_MIN or args.seed_start + args.cases_count - 1 > SEED_MAX:
        cli.error('All literal seeds must remain within 1180000-1189999')
    if args.styles and len(set(args.styles)) != len(args.styles):
        cli.error('--styles must not repeat names')
    try:
        generate(args)
    except (OSError, ValueError) as exc:
        cli.error(str(exc))


if __name__ == '__main__':
    main()
