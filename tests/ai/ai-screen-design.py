#!/usr/bin/env python3
"""Fixed-level factorial and balanced mixed-seat AI screens (never a promotion gate).

Examples:
  python3 tests/ai/ai-screen-design.py factorial --spec factors.json --output design
  python3 tests/ai/ai-screen-design.py tournament --variants style-a style-b style-c \
      --formats ffa4 ffa6 --seeds 17001 17002 --output tournament
  python3 tests/ai/ai-screen-design.py pilot --output pilot --source-root .

Factor spec: {"factors":[{"name":"Reserve", "section":"Force Concentration",
"key":"VanillaFfaReserve"}], "fixed":{"Force Concentration":{"OtherKey":0}}}.
Only hand-picked existing boolean switches; fixed numeric levels are not searched.
The generated plan contains literal argv arrays, common-random-number cases, an
explicit native arm, alias classes, and honest game-count/coverage accounting.
"""
import argparse
import hashlib
import itertools
import json
from math import comb
from pathlib import Path
import random
import re
import shlex

FORMATS = ('1v1', '2v2', 'ffa4', 'ffa6')
SPICES = ('low', 'medium', 'high')
# Random Skirmish terrain uses 5% steps summing to 100 (SkirmishGenerator
# kTerrainStep); scenario-match rejects any recipe the engine would round.
COMPOSITIONS = ({'name': 'scarce-rock', 'rock': 15, 'mountain': 5},
                {'name': 'standard', 'rock': 30, 'mountain': 5},
                {'name': 'rocky-ridges', 'rock': 45, 'mountain': 15})
MAP_SIZES = (64, 96, 128, 160, 192, 256)


def validate_cases(manifest):
    """Reject cells the probe would clamp, before any arm is queued."""
    if manifest.get('schema_version') != 1 or not manifest.get('cases'):
        raise ValueError('Case manifest needs schema_version=1 and nonempty cases')
    if not 0 <= int(manifest['sample_seed']) <= 0xffffffff:
        raise ValueError('Case manifest sample_seed must be unsigned 32-bit')
    # A seed may recur across formats (different rosters make different maps), never within one.
    seen = set()
    for index, cell in enumerate(manifest['cases'], 1):
        rock, mountain = int(cell.get('rock', 30)), int(cell.get('mountain', 5))
        seed = int(cell['seed'])
        if (cell['format'] not in FORMATS or int(cell['size']) not in MAP_SIZES
                or cell['spice_level'] not in SPICES or not 1 <= int(cell['initial_tech']) <= 9
                or not 0 <= seed <= 0xffffffff or (cell['format'], seed) in seen
                or rock % 5 or mountain % 5 or rock < 5 or mountain < 0 or rock + mountain > 95):
            raise ValueError(f'Unsupported or duplicate case {index}: {cell}')
        seen.add((cell['format'], seed))
    return manifest


def fractional_columns(k):
    """Odd-weight binary columns are sum-free: no relation of length <4.

    For eight switches this is 2^(8-4), 16 rows, resolution IV. For <=3
    switches use a full factorial. This constructs a design, not a parameter search.
    """
    if k < 1 or k > 32:
        raise ValueError('Choose 1-32 fixed boolean factors')
    m = 1
    while 2 ** (m - 1) < k:
        m += 1
    basis = [1 << i for i in range(m)]
    rest = [mask for mask in range(1, 1 << m)
            if bin(mask).count('1') % 2 and mask not in basis]
    return m, (basis + rest)[:k]


def resolution(columns):
    for size in range(2, len(columns) + 1):
        for group in itertools.combinations(columns, size):
            value = 0
            for mask in group:
                value ^= mask
            if not value:
                return size
    return 'full-factorial'


def alias_groups(factors, columns):
    groups = {}
    for i, j in itertools.combinations(range(len(factors)), 2):
        column = columns[i] ^ columns[j]
        groups.setdefault(column, []).append(factors[i]['name'] + ':' + factors[j]['name'])
    return [{'column': column, 'terms': terms, 'individually_identifiable': len(terms) == 1}
            for column, terms in sorted(groups.items())]


def cases(seed, formats, sizes, spices, techs, replicate=1):
    """Cross format x size x spice; Latin-balanced tech/composition across it.

    Tech/composition are not a full crossing; the coverage table makes every
    untested joint cell explicit. Each replicate uses fresh seeds. Same cases
    are copied unchanged into every arm, with both candidate seats by the audit.
    """
    rng = random.Random(seed)
    rows, used = [], set()
    for rep, fmt, si, pi in itertools.product(range(replicate), formats,
                                             range(len(sizes)), range(len(spices))):
        fi = formats.index(fmt)
        ti = (si + pi + fi + rep) % len(techs)
        ci = (si + 2 * pi + fi + rep) % len(COMPOSITIONS)
        game_seed = rng.getrandbits(32)
        while game_seed in used:
            game_seed = rng.getrandbits(32)
        used.add(game_seed)
        comp = COMPOSITIONS[ci]
        rows.append(dict(format=fmt, size=sizes[si], spice_level=spices[pi],
                         initial_tech=techs[ti], difficulty='brutal', seed=game_seed,
                         rock=comp['rock'], mountain=comp['mountain'],
                         composition=comp['name'], replicate=rep))
    rng.shuffle(rows)
    return {'schema_version': 1, 'sample_seed': seed, 'cases': rows}


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def coverage(rows):
    result = {}
    for factor in ('format', 'size', 'spice_level', 'initial_tech', 'composition'):
        counts = {}
        for row in rows:
            key = str(row[factor])
            counts[key] = counts.get(key, 0) + 1
        result[factor] = counts
    result['joint_format_size_spice'] = [dict(format=f, size=s, spice_level=p,
        cases=sum(row['format'] == f and row['size'] == s and row['spice_level'] == p for row in rows))
        for f, s, p in itertools.product(FORMATS, sorted({r['size'] for r in rows}), SPICES)]
    return result


def ini_text(spec, levels):
    values = {section: dict(settings) for section, settings in spec.get('fixed', {}).items()}
    for factor in spec['factors']:
        section = factor.get('section', 'Force Concentration')
        key = factor['key']
        values.setdefault(section, {})[key] = factor.get('on', 1) if levels[factor['name']] else factor.get('off', 0)
    lines = ['# Fixed factorial composite; see the preregistered design JSON, not an automatic search.']
    for section, settings in values.items():
        lines.append('[' + section + ']')
        for key, value in settings.items():
            if isinstance(value, bool):
                value = 'true' if value else 'false'
            lines.append(f'{key}={value}')
        lines.append('')
    return '\n'.join(lines)


def base_command(args, case_file, result):
    return ['python3', 'tests/ai/dunecity-audit.py', '--mode', 'sample', '--mod', args.mod,
            '--case-manifest', str(case_file), '--minutes', str(args.minutes),
            '--base', args.base, '--army', '2', '--city', '0', '--jobs', '1',
            '--export-maps', 'maps', '--results', str(result)]


def explicit_cases(args):
    """An explicit manifest (e.g. a supplement replacing rejected cells) or the generated grid."""
    if args.cases:
        return validate_cases(json.loads(args.cases.read_text()))
    return validate_cases(cases(args.seed, args.formats, args.sizes, args.spices, args.techs,
                                getattr(args, 'replicates', 1)))


def factorial(args, spec=None):
    spec = spec or json.loads(args.spec.read_text())
    factors = spec['factors']
    names = [factor['name'] for factor in factors]
    keys = [(factor.get('section', 'Force Concentration'), factor['key']) for factor in factors]
    if len(set(names)) != len(names) or len(set(keys)) != len(keys):
        raise ValueError('Factor names and section/key pairs must be unique')
    for factor in factors:
        if factor.get('off', 0) not in (0, False, 'false') or factor.get('on', 1) not in (1, True, 'true'):
            raise ValueError('Factorial factors must be existing hashed bool switches at off/on levels')
    m, columns = fractional_columns(len(factors))
    if args.dealias_pairs:
        if len(factors) < 6 or len(factors) > 10:
            raise ValueError('--dealias-pairs supports 6-10 hand-picked factors')
        m = len(factors) - 1
        # A single even defining word of length >=6 leaves every two-factor
        # term distinct. Six factors use ABCDEF=+1: 32 runs, resolution VI.
        word = (1 << (m if len(factors) % 2 == 0 else m - 1)) - 1
        columns = [1 << i for i in range(m)] + [word]
    root = args.output.resolve()
    case_path = root / 'cases.json'
    case_data = explicit_cases(args)
    write_json(case_path, case_data)
    source_case = Path('tests/ai/screen-designs') / ((args.case_label or args.prefix) + '-cases.json')
    write_json(args.source_root / source_case, case_data)
    runs, commands = [], []
    for row in range(1 << m):
        levels = {name: int(bin(row & mask).count('1') % 2 == 0) for name, mask in zip(names, columns)}
        style = f'{args.prefix}-{row:02d}'
        path = args.source_root / 'tests/ai/play-styles' / (style + '.ini')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(ini_text(spec, levels))
        result = root / 'runs' / style
        command = base_command(args, source_case, result) + ['--candidate-style', style]
        runs.append(dict(style=style, levels=levels, row=row, result_dir=str(result),
                         ini_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        commands.append(dict(style=style, argv=command, games=2 * len(case_data['cases'])))
    control = dict(style='control', levels={name: 0 for name in names},
                   result_dir=str(root / 'runs/native'))
    commands.append(dict(style='control', argv=base_command(args, source_case, Path(control['result_dir'])),
                         games=2 * len(case_data['cases'])))
    games_per_arm = 2 * len(case_data['cases'])
    plan = dict(schema_version=1, kind='fractional-factorial', seed=args.seed, sample_seed=case_data['sample_seed'],
                source_base=args.source_base, factors=factors, fixed=spec.get('fixed', {}),
                columns=dict(zip(names, columns)), resolution=resolution(columns),
                interaction_aliases=alias_groups(factors, columns), runs=runs, arms=runs + [control],
                case_manifest=str(case_path), coverage=coverage(case_data['cases']), commands=commands,
                game_counts=dict(factorial=len(commands) * games_per_arm,
                    full_factorial=(2 ** len(factors) + 1) * games_per_arm,
                    minimal_oat=(len(factors) + 1) * games_per_arm,
                    oat_equal_per_factor_exposure=(len(factors) + 1) * (len(runs) // 2) * games_per_arm),
                inference_note='Main effects unaliased with two-factor terms; two-factor alias classes are not individual pairs. Native arm is a gate comparator, not another factorial row. Fixed controls still required.')
    write_json(root / 'design.json', plan)
    (root / 'commands.sh').write_text('\n'.join(shlex.join(item['argv']) for item in commands) + '\n')
    print(json.dumps(dict(design=str(root / 'design.json'), resolution=plan['resolution'],
                          runs=len(runs), cases=len(case_data['cases']), **plan['game_counts']), indent=2))
    return plan


def tournament(args):
    variants = args.variants
    if len(set(variants)) != len(variants) or 'control' in variants:
        raise ValueError('Supply unique non-control variants; native control is added explicitly')
    if len(variants) < 1:
        raise ValueError('Supply at least one existing variant')
    if any(fmt not in ('ffa4', 'ffa6') for fmt in args.formats):
        raise ValueError('Tournament is FFA4/FFA6 only')
    if args.cases:
        manifest = validate_cases(json.loads(args.cases.read_text()))
        base_seed = int(manifest['sample_seed'])
        rows_by_format = {fmt: [cell for cell in manifest['cases'] if cell['format'] == fmt]
                          for fmt in args.formats}
    else:
        if not args.seeds or len(set(args.seeds)) != len(args.seeds):
            raise ValueError('Use --cases FILE or distinct --seeds')
        base_seed = args.seed
        rows_by_format = {}
        for fmt in args.formats:
            rows_by_format[fmt] = [dict(format=fmt, size=args.sizes[i % len(args.sizes)],
                spice_level=args.spices[(i // len(args.sizes)) % len(args.spices)],
                initial_tech=args.techs[(i + i // len(args.sizes)) % len(args.techs)],
                difficulty='brutal', seed=seed, composition=COMPOSITIONS[(i + 2 * (i // len(args.sizes))) % 3]['name'],
                **{key: COMPOSITIONS[(i + 2 * (i // len(args.sizes))) % 3][key] for key in ('rock', 'mountain')})
                for i, seed in enumerate(args.seeds)]
    if not all(rows_by_format.values()):
        raise ValueError('Every tournament format needs at least one case')
    planned = sum(comb(len(variants), min(int(fmt[-1]) - 1, len(variants)))
                  * int(fmt[-1]) * len(rows_by_format[fmt]) for fmt in args.formats)
    if planned > args.max_games:
        raise ValueError(f'Exact pair-balanced design needs {planned} games, above --max-games={args.max_games}; choose fewer hand-picked variants or explicitly raise the limit')
    root = args.output.resolve()
    commands, assignment_rows = [], []
    label = args.case_label or f'tournament-{args.seed}'
    for fmt in args.formats:
        seats = int(fmt[-1])
        # Formats get distinct sample seeds, so their case numbers never share a CRN cluster key.
        case_data = validate_cases(dict(schema_version=1, sample_seed=(base_seed + seats) & 0xffffffff,
                                        cases=rows_by_format[fmt]))
        write_json(root / f'cases-{fmt}.json', case_data)
        source_case = Path('tests/ai/screen-designs') / f'{label}-{fmt}.json'
        write_json(args.source_root / source_case, case_data)
        take = min(seats - 1, len(variants))
        # All same-sized subsets give exact pair balance (an incomplete block
        # design). A cyclic-only cohort schedule does not balance opponents.
        for ci, cohort in enumerate(itertools.combinations(variants, take)):
            labels = ['control'] + list(cohort)
            labels += ['control'] * (seats - len(labels))
            seeds = [cell['seed'] for cell in case_data['cases']]
            result = root / 'runs' / f'{fmt}-cohort-{ci:03d}'
            command = base_command(args, source_case, result) + ['--seat-styles', ','.join(labels)]
            commands.append(dict(format=fmt, cohort=list(cohort), result_dir=str(result),
                                 sample_seed=case_data['sample_seed'], argv=command, games=len(seeds) * seats))
            for seed in seeds:
                for rotation in range(seats):
                    assignment_rows.append(dict(format=fmt, seed=seed, cohort=ci, rotation=rotation,
                        seats=[labels[(seat + rotation) % seats] for seat in range(seats)]))
    total = sum(item['games'] for item in commands)
    plan = dict(schema_version=1, kind='mixed-seat-tournament', variants=variants,
                source_base=args.source_base, commands=commands, assignments=assignment_rows,
                games=total, balance_note='Every variant appears at every physical seat for every selected seed. Every pair of non-control variants meets equally often within each format. Native controls fill spare seats and therefore are overrepresented; same-game house histories are not independent trials. Each command uses the harness --seat-styles control,A,B,... syntax and its complete seat rotations.')
    write_json(root / 'tournament.json', plan)
    (root / 'commands.sh').write_text('\n'.join(shlex.join(item['argv']) for item in commands) + '\n')
    print(json.dumps(dict(design=str(root / 'tournament.json'), games=total, commands=len(commands)), indent=2))
    return plan


def pilot_spec():
    # Refinery and greedy have an explicitly supported compound at the source
    # pin. Harvester/reactive are alternate boom families, so are not encoded
    # as simultaneously enabled boolean factors. FFA-only scope is fixed on.
    names = [('refinery6', 'VanillaRefineryBoom'), ('greedy6', 'VanillaGreedyBoom'),
             ('reserve', 'VanillaFfaReserve'), ('full_waves', 'VanillaFfaFullWaves'),
             ('single_front', 'VanillaFfaSingleFront'), ('funded_tech', 'VanillaFfaFundedTech')]
    fixed = dict(VanillaHarvesterBoom=0, VanillaReactiveBoom=0, VanillaBoomFfaOnly=1,
                 VanillaHarvesterBoomWorkers=8, VanillaRefineryBoomRefineries=6,
                 VanillaGreedyBoomMinutes=6, VanillaReactiveArmyRatioBps=6000,
                 VanillaReactiveEconomyRatioBps=12000, VanillaFfaWeakestTarget=0,
                 VanillaFfaNearbyTarget=0, VanillaFfaStarportAfterWave=0,
                 VanillaFfaThreatProduction=0, VanillaFfaLightWorkingCash=0)
    return dict(factors=[dict(name=name, section='Force Concentration', key=key) for name, key in names],
                fixed={'Force Concentration': fixed})


def parser():
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subs = result.add_subparsers(dest='kind', required=True)
    for name in ('factorial', 'pilot', 'tournament'):
        sub = subs.add_parser(name)
        sub.add_argument('--output', type=Path, required=True)
        sub.add_argument('--source-root', type=Path, default=Path('.'))
        sub.add_argument('--source-base', default='235bb65e8d22c2fc1e35f61c1b02e15a9a6f6aa3')
        sub.add_argument('--seed', type=int, default=2026110807)
        sub.add_argument('--cases', type=Path,
                         help='Explicit schema-v1 case manifest instead of the generated grid (for example a supplement)')
        sub.add_argument('--case-label', help='Name of the pinned tests/ai/screen-designs case file(s)')
        sub.add_argument('--formats', nargs='+', choices=FORMATS,
                         default=['ffa4', 'ffa6'] if name == 'tournament' else list(FORMATS))
        sub.add_argument('--sizes', nargs='+', type=int, default=[64, 128, 160])
        sub.add_argument('--spices', nargs='+', choices=SPICES, default=list(SPICES))
        sub.add_argument('--techs', nargs='+', type=int, choices=range(1, 10), default=[3, 6, 9])
        sub.add_argument('--minutes', type=int, default=90)
        sub.add_argument('--base', choices=('cy', 'small', 'medium', 'large', 'fortress'), default='medium')
        sub.add_argument('--mod', choices=('vanilla', 'dunecity'), default='vanilla')
        if name == 'tournament':
            sub.add_argument('--variants', nargs='+', required=True)
            sub.add_argument('--seeds', nargs='+', type=int)
            sub.add_argument('--max-games', type=int, default=10000)
        else:
            sub.add_argument('--replicates', type=int, default=1)
            sub.add_argument('--dealias-pairs', action='store_true', default=name == 'pilot',
                             help='Use a resolution-VI-or-better half fraction so individual two-factor terms are identifiable')
            sub.add_argument('--prefix', default='stats-vanilla-r4')
            if name == 'factorial':
                sub.add_argument('--spec', type=Path, required=True)
    return result


def validate_switches(spec, source_root):
    source = (source_root / 'src/players/QuantBotConfig.cpp').read_text()
    for factor in spec['factors']:
        section = re.escape(factor.get('section', 'Force Concentration'))
        key = re.escape(factor['key'])
        match = re.search(r'([\w.]+)\s*=\s*iniFile\.getBoolValue\("'
                          + section + r'",\s*"' + key + r'"', source)
        if not match or not re.search(r'addNamed\("[^"]+",\s*'
                                      + re.escape(match.group(1)) + r'\)', source):
            raise ValueError(f'Not an existing hashed boolean at this source: {factor["key"]}')


def main():
    args = parser().parse_args()
    if not 1 <= args.minutes <= 120 or any(size < 32 or size > 256 for size in args.sizes):
        raise SystemExit('Use 1-120 minutes and map sizes 32-256')
    if not 0 <= args.seed <= 0xffffffff or getattr(args, 'replicates', 1) < 1:
        raise SystemExit('Use an unsigned 32-bit seed and positive replicates')
    if args.kind == 'tournament':
        tournament(args)
    else:
        spec = pilot_spec() if args.kind == 'pilot' else json.loads(args.spec.read_text())
        validate_switches(spec, args.source_root)
        factorial(args, spec)


if __name__ == '__main__':
    main()
