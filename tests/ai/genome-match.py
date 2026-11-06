#!/usr/bin/env python3
"""Per-house QuantBot genome league, submitted through dunecity-queue --rev.

The inline --manifest-b64 is the entire schema_version=1 JSON manifest. Generic
schedule/build_probe/play distribute individual matches; all games use leased
scratch. --report only reads results; --scores merges the canonical BaselineLeague
observations, never a separate behaviour scorecard. Traces, maps, full effective
options and resolved phase INIs are retained compressed before scratch cleanup.
"""
import argparse
import ast
import base64
import configparser
import csv
import gzip
import hashlib
import itertools
import json
import os
import shutil
import subprocess
import sys
import time
import types
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from functools import lru_cache
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/ai'))
sys.path.insert(0, str(root / 'tests/performance'))
from probe_profile import finish_game, probe_binary, require_free_space, scratch_dir  # noqa: E402
import genome_model  # noqa: E402
import match_score  # noqa: E402


def helpers(filename):
    """Reuse existing non-importable harnesses through the queue's AST boundary."""
    path = Path(__file__).with_name(filename)
    nodes = []
    for node in ast.parse(path.read_text(encoding='utf-8'), str(path)).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        nodes.append(node)
    else:
        raise RuntimeError('Missing queue argument boundary: ' + str(path))
    namespace = {'__file__': str(path), '__name__': 'cluster_harness'}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), 'exec'), namespace)
    return types.SimpleNamespace(**namespace)


audit = helpers('dunecity-audit.py')
vanilla = helpers('vanilla-match.py')
read = audit.read
PHASES = ('opening', 'mid', 'late')
ALL_HOUSES = ('harkonnen', 'atreides', 'ordos', 'fremen', 'sardaukar', 'mercenary',
              'neutral', 'rebels', 'custom', 'wildspade', 'kleshmersh', 'tharpique')
DIFFICULTIES = ('defend', 'easy', 'medium', 'hard', 'brutal')
MODS = {'vanilla': 'vanilla', 'dune2r': 'Dune2R', 'dunecity': 'dunecity'}
BASES = {'none': 0, 'cy': 0, 'outpost': 1, 'small': 2, 'medium': 3, 'large': 4, 'fortress': 5}
SPICE = {'none': 0, 'low': 1, 'medium': 2, 'high': 3, 'very-high': 4}
GAME_FIELDS = audit.GAME_FIELDS + (
    'tag', 'mod', 'research', 'technology_installments', 'map', 'map_width', 'map_height', 'context_id', 'comparison_id',
    'manifest_sha256', 'registry_sha256', 'manifest_b64', 'phase_schedule_sha256', 'phase_schedule_json',
    'phase_config_hashes', 'map_sha256', 'options_sha256', 'options_json', 'requested_options_sha256',
    'winner', 'winner_label', 'end_s', 'cycle', 'evidence', 'observations', 'fixtures', 'hash_scope')
HOUSE_FIELDS = audit.HOUSE_FIELDS + ('genome_id', 'genome_digest', 'slot', 'comparison_id', 'research', 'technology_installments', 'mod')
ASSIGNMENT_FIELDS = ('id', 'context_id', 'house', 'slot', 'genome_id', 'genome_digest', 'comparison_id', 'difficulty',
                     'mod', 'format', 'size', 'base', 'spice_level', 'research', 'technology_installments',
                     'manifest_sha256', 'phase_schedule_sha256')
PHASE_FIELDS = ('id', 'house', 'genome_id', 'phase', 'start_minute', 'start_cycle', 'applied_cycle', 'config_hash', 'ini_sha256', 'applied')
# Lean evidence also always writes games, assignments, config phases and scorecard observations.
LEAN_TABLES = ('houses.csv',)
OBSERVATION_EXTRA = ('game_id', 'genome_id', 'genome_digest', 'comparison_id', 'format', 'size', 'base', 'spice_level',
                     'phase', 'manifest_sha256', 'phase_schedule_sha256')
SAMPLE_FIELDS = audit.SAMPLE_FIELDS + ('refineries', 'zone_residential', 'zone_industrial', 'zone_commercial',
                                      'tech_level', 'light', 'light_built', 'palace', 'palace_built')
UNIT_FIELDS = audit.META + ('minute', 'cycle', 'house', 'item', 'built', 'lost', 'actual', 'live_value')


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def digest(value):
    return hashlib.sha256(canonical(value).encode('utf-8')).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


@lru_cache(maxsize=8)
def manifest(encoded):
    if not isinstance(encoded, str) or not encoded:
        raise ValueError('--manifest-b64 is required to schedule or play games')
    try:
        raw = base64.b64decode(encoded + '=' * (-len(encoded) % 4), altchars=b'-_', validate=True)
        value = json.loads(raw.decode('utf-8'), object_pairs_hook=unique_object)
    except (ValueError, UnicodeError) as error:
        raise ValueError('--manifest-b64 must encode UTF-8 JSON: ' + str(error)) from error
    if (not isinstance(value, dict) or set(value) != {'schema_version', 'genomes'}
            or type(value['schema_version']) is not int or value['schema_version'] != 1
            or not isinstance(value['genomes'], list)):
        raise ValueError('Manifest must be {"schema_version":1,"genomes":[...]}')
    registry, genomes = genome_model.load_registry(), {}
    for genome in value['genomes']:
        genome_model.validate_genome(genome, registry)
        if genome['id'] in genomes:
            raise ValueError('Duplicate genome id: ' + genome['id'])
        genomes[genome['id']] = genome
    if 'default' not in genomes:
        default = genome_model.default_genome(registry)
        default['id'] = 'default'
        genomes['default'] = default
    value = {'schema_version': 1, 'genomes': [genomes[key] for key in sorted(genomes)]}
    inline = base64.b64encode(canonical(value).encode('utf-8')).decode('ascii')
    return value, genomes, registry, inline


def validate(args):
    _, genomes, _, _ = manifest(args.manifest_b64)
    formats = args.formats or (['1v1'] if args.pairs else ['ffa'])
    if not args.pairs and not args.ffa:
        raise ValueError('Give --pairs, --ffa, or both with --formats')
    if any(fmt == '1v1' and not args.pairs or fmt == 'ffa' and not args.ffa for fmt in formats):
        raise ValueError('Every --formats mode needs its --pairs or --ffa roster')
    if not args.seeds:
        raise ValueError('--seeds is required to schedule or play games')
    if len(set(args.seeds)) != len(args.seeds) or any(not 0 <= seed <= 0xffffffff for seed in args.seeds):
        raise ValueError('Seeds must be distinct unsigned 32-bit integers')
    if len(set(args.houses or ())) != len(args.houses or ()):
        raise ValueError('Playing houses must be distinct')
    if args.ffa and len(args.houses or vanilla.FFA_HOUSES) != 4:
        raise ValueError('Genome FFA uses exactly four playing houses')
    if args.houses and len(args.houses) < 2:
        raise ValueError('Pairs need at least two playing houses')
    if args.phase_minutes[0] != 0 or not 0 < args.phase_minutes[1] < args.phase_minutes[2] <= 10000:
        raise ValueError('--phase-minutes must be 0 MID LATE, increasing integer game minutes')
    if not 1 <= args.minutes <= 120 or args.jobs < 1 or args.wall_timeout < 1:
        raise ValueError('Use 1-120 minutes and positive jobs/wall timeout')
    if not 256 <= args.capture_mib <= 4096:
        raise ValueError('Use 256-4096 MiB of telemetry capture')
    if args.game_options and not args.game_options.is_file():
        raise ValueError('--game-options must name an existing INI')
    labels = []
    if args.pairs:
        for pair in args.pairs.split(','):
            parts = pair.split(':')
            if len(parts) != 2 or not all(parts):
                raise ValueError('--pairs needs genome:genome[,genome:genome]')
            labels.extend(parts)
    if args.ffa:
        quartet = args.ffa.split(',')
        if len(quartet) not in (1, 2, 4) or not all(quartet):
            raise ValueError('--ffa needs one self-play, two balanced, or four genome IDs')
        labels.extend(quartet)
    if set(labels) - set(genomes):
        raise ValueError('Unknown genome IDs: ' + ', '.join(sorted(set(labels) - set(genomes))))
    return formats


def map_info(name):
    generated = vanilla.skirmish(name)
    if generated:
        size, mod = generated
        if size not in (64, 96, 128, 160, 192, 256):
            raise ValueError('Unsupported generated size: ' + name)
        return size, size, mod
    config = configparser.ConfigParser(strict=False, interpolation=None)
    text = vanilla.map_path(name).read_text(encoding='utf-8', errors='replace')
    config.read_string('\n'.join(line for line in text.splitlines()
                                if not line.strip() or line.replace('\t', ' ').isprintable()))
    section = next((section for section in config.sections() if section.lower() == 'map'), None)
    if section is None:
        raise ValueError('Map has no dimensions: ' + name)
    return config.getint(section, 'SizeX'), config.getint(section, 'SizeY'), None


def orientations(args, fmt):
    if fmt == '1v1':
        for pair_houses in itertools.combinations(args.houses or ('harkonnen', 'atreides', 'ordos'), 2):
            for pair in args.pairs.split(','):
                labels = pair.split(':')
                for assignment in dict.fromkeys((tuple(labels), tuple(reversed(labels)))):
                    for order in (pair_houses, tuple(reversed(pair_houses))):
                        yield list(zip(order, assignment))
    else:
        houses, labels = list(args.houses or vanilla.FFA_HOUSES), args.ffa.split(',')
        if len(labels) == 1:
            assignments = [labels * 4]
        elif len(labels) == 2:
            assignments = [[labels[0] if index in group else labels[1] for index in range(4)]
                           for group in itertools.combinations(range(4), 2)]
        else:
            assignments = [labels[shift:] + labels[:shift] for shift in range(4)]
        for assignment in assignments:
            for shift in range(4):
                yield list(zip(houses[shift:] + houses[:shift], assignment))


def schedule(args):
    formats = validate(args)
    value, genomes, registry, inline = manifest(args.manifest_b64)
    manifest_sha, registry_sha = digest(value), digest(registry)
    options_sha = file_digest(args.game_options) if args.game_options else ''
    games, seen = [], set()
    for mod, size, difficulty, fmt, seed, base, spice in itertools.product(
            args.mods, args.sizes, args.difficulties, formats, args.seeds, args.bases, args.spice):
        for name in args.maps or [f'rs{size}-{mod}']:
            width, height, map_mod = map_info(name)
            if map_mod and (map_mod != mod or width != size):
                continue
            if not map_mod and size != args.sizes[0]:
                continue
            army = args.army if args.army is not None else (5 if base == 'fortress' else 0)
            city = args.city if args.city is not None else (4 if base == 'fortress' and mod == 'dunecity' else 0)
            if mod != 'dunecity' and city:
                raise ValueError('City starts require --mods dunecity')
            tech = args.tech if args.tech is not None else (1 if args.research == 'on' else 9)
            for sides in orientations(args, fmt):
                context = dict(tag=args.tag, mod=mod, difficulty=difficulty, format=fmt, seed=seed,
                               map=name, map_width=width, map_height=height, size=max(width, height),
                               base=base if map_mod else 'map', army=army if map_mod else '', city=city if map_mod else '',
                               spice_level=spice if map_mod else 'map', research=args.research,
                               technology_installments=args.technology_installments, research_pace=args.research_pace,
                               initial_tech=tech, limit=args.minutes, requested_options_sha256=options_sha)
                match_context = {key: value for key, value in context.items() if key != 'tag'}
                context_id = digest(dict(match_context, houses=[house for house, _ in sides]))
                comparison = dict(match_context, phase_minutes=args.phase_minutes,
                                  houses=sorted(house for house, _ in sides),
                                  roster=sorted((label, genome_model.genome_id(genomes[label])) for _, label in sides))
                identity = dict(context, sides=sides, manifest_sha256=manifest_sha,
                                registry_sha256=registry_sha, phase_minutes=args.phase_minutes)
                game_id = 'g' + digest(identity)[:32]
                if game_id not in seen:
                    seen.add(game_id)
                    games.append(dict(context, id=game_id, context_id=context_id, comparison_id=digest(comparison),
                                      sides=sides, manifest_sha256=manifest_sha, registry_sha256=registry_sha,
                                      manifest_b64=inline, variant=manifest_sha, style='genome', minutes=args.minutes))
    if not games:
        raise ValueError('No maps match the requested mods/sizes/formats')
    return games


def house_number(house):
    if type(house) is int and 0 <= house < len(ALL_HOUSES):
        return house
    if isinstance(house, str) and house in ALL_HOUSES:
        return ALL_HOUSES.index(house)
    raise ValueError('Unknown genome house: ' + str(house))


def render_phase_inputs(assignments, context, genomes, directory, phase_minutes=(0, 10, 20), registry=None,
                        elapsed=False, difficulty_variants=False):
    """Central phase transport for ordinary matches and unchanged saved rosters.

    Assignments are (house-name-or-number, genome-id). elapsed=True starts opening
    at the loaded cycle. difficulty_variants=True emits all five numeric bot
    difficulties, letting the probe select each existing controller's real tier.
    Non-QuantBot/shared human houses are never overridden by the C++ wrapper.
    """
    if (len(phase_minutes) != 3 or phase_minutes[0] != 0
            or not 0 < phase_minutes[1] < phase_minutes[2] <= 10000
            or any(type(minute) is not int for minute in phase_minutes)):
        raise ValueError('Genome phases need three increasing integer starts: 0 MID LATE')
    assignments = list(assignments)
    if len({house_number(house) for house, _ in assignments}) != len(assignments):
        raise ValueError('Duplicate genome house assignment')
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    lines = ['[Schedule]', 'elapsed=' + str(elapsed).lower()]
    lines += [f'{phase}={minute}' for phase, minute in zip(PHASES, phase_minutes)]
    lines += ['', '[Genomes]'] + [f'{house_number(house)}={label}' for house, label in assignments]
    receipts = []
    for house, label in assignments:
        number = house_number(house)
        variants = list(enumerate(DIFFICULTIES)) if difficulty_variants else [(None, context['difficulty'])]
        for index, difficulty in variants:
            for phase, minute in zip(PHASES, phase_minutes):
                expressed = dict(context, difficulty=difficulty, house=ALL_HOUSES[number], phase=phase)
                payload = genome_model.render_ini(genomes[label], expressed, registry).encode('utf-8')
                stem = str(number) if index is None else f'{number}-{index}'
                (directory / f'{stem}-{phase}.ini').write_bytes(payload)
                receipts.append(dict(house=number, genome_id=label, phase=phase,
                                     start_minute=minute, ini_sha256=hashlib.sha256(payload).hexdigest(), context=expressed))
    (directory / 'schedule.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return receipts


def phase_inputs(args, game, directory):
    _, genomes, registry, _ = manifest(args.manifest_b64)
    context = {key: game[key] for key in ('difficulty', 'mod', 'format', 'size')}
    receipts = render_phase_inputs(game['sides'], context, genomes, directory, args.phase_minutes, registry)
    with (directory / 'schedule.ini').open('a', encoding='utf-8') as stream:
        stream.write('\n[Map]\n' + '\n'.join((f'base={BASES.get(game["base"], 0)}', f'army={game["army"] or 0}',
                     f'city={game["city"] or 0}', f'tech={game["initial_tech"]}',
                     f'spice={SPICE.get(game["spice_level"], 2)}', f'deploy_mcvs={str(game["base"] == "cy").lower()}')) + '\n')
    options = configparser.ConfigParser(strict=False, interpolation=None)
    options.optionxform = str
    if args.game_options:
        options.read(args.game_options, encoding='utf-8')
    if not options.has_section('Game Options'):
        options.add_section('Game Options')
    overrides = {'Research Lab': args.research == 'on'}
    if args.technology_installments is not None:
        overrides['Technology Installments'] = args.technology_installments == 'on'
    if args.research_pace is not None:
        overrides['Research Pace'] = args.research_pace
    for key, value in overrides.items():
        for old in list(options['Game Options']):
            if old.lower() == key.lower():
                del options['Game Options'][old]
        options['Game Options'][key] = str(value).lower()
    with (directory / 'requested-options.ini').open('w', encoding='utf-8', newline='\n') as stream:
        options.write(stream)
    return receipts


def command(args, binary, game, out, inputs):
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--mod', MODS[game['mod']],
           '--match-sides', ','.join(f'{house}:default' for house, _ in game['sides']), '--house', game['sides'][0][0],
           '--local-spectator', '--enemy-difficulty', game['difficulty'], '--partner-difficulty', game['difficulty'],
           '--minutes', str(game['limit']), '--seed', str(game['seed']), '--capture-mib', str(args.capture_mib),
           '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--genome-phase-dir', str(inputs),
           '--game-options', str(inputs / 'requested-options.ini')]
    generated = vanilla.skirmish(game['map'])
    return cmd + (['--skirmish-ffa-size', str(generated[0]), '--skirmish-map-seed', str(game['seed'])] if generated else
                  ['--custom-map', str(vanilla.map_path(game['map']))])


def build_probe(args, scratch):
    if args.prebuilt_probe:
        return Path(args.prebuilt_probe)
    out = Path(scratch) / 'probe'
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
                    '--output-dir', str(out), '--custom-map', str(vanilla.map_path('x-factor')),
                    '--match-sides', 'harkonnen:default,atreides:default', '--local-spectator',
                    '--enemy-difficulty', 'hard', '--partner-difficulty', 'hard', '--build-only'],
                   cwd=root, check=True, capture_output=True, text=True)
    return probe_binary(out)


def markers(lines, marker):
    return [audit.parse(line, marker + ':') for line in lines if marker + ':' in line]


def metadata(game):
    return {field: game[field] for field in audit.META}


def compressed_copy(source, destination):
    with Path(source).open('rb') as stream, Path(destination).open('wb') as raw:
        with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as target:
            shutil.copyfileobj(stream, target, 1024 * 1024)


def retain_evidence(results, out, inputs, game, full=True):
    prefix = 'evidence-' + game['id']
    paths = ([(out / 'run.log', 'run.log')] if full else []) + [(out / 'summary.json', 'summary.json')]
    if full:
        paths += [(path, f'events-{index}.jsonl') for index, path in enumerate(sorted((out / 'profile').rglob('events.jsonl')))]
        paths += [(path, f'replay-{index}.rpl') for index, path in enumerate(sorted((out / 'profile').rglob('*.rpl')))]
    paths += [(path, 'input-' + path.name) for path in sorted(inputs.glob('*.ini'))]
    receipts = []
    for source, name in paths:
        if source.is_file():
            target = results / (prefix + '-' + name + '.gz')
            compressed_copy(source, target)
            receipts.append(dict(file=target.name, sha256=file_digest(target), source_sha256=file_digest(source),
                                 source_bytes=source.stat().st_size))
    path = results / (prefix + '.json')
    path.write_text(canonical(dict(schema_version=1, id=game['id'], files=receipts)) + '\n', encoding='utf-8')
    return path.name


def tables_for_game(args, game, lines, summary, final, samples, capture):
    meta, groups = metadata(game), defaultdict(list)
    for sample in samples:
        groups[int(sample['house'])].append(sample)
    attacks, waves = defaultdict(list), []
    for event in summary.get('attacks', []):
        data = event['data']
        if int(data['members']) > 0:
            house, cycle = int(event['house']), int(event['cycle'])
            attacks[house].append(cycle / audit.CYCLES_PER_MINUTE)
            waves.append(dict(meta, house=house, cycle=cycle, members=data['members'], value=data['value']))
    eliminated = {int(house['house']): float(house['eliminated_s']) for house in final if house['alive'] == '0'}
    teams = {int(house['house']): int(house['team']) for house in final}
    scores = match_score.score_game(list(teams), eliminated, teams=teams)
    air_cycle = max((int(event['cycle']) for event in capture['events']), default=0)
    air_final = {int(house['house']): dict(house=house['house'], cycle=air_cycle) for house in final}
    audit.add_air_kill_samples(list(air_final.values()), capture)
    _, genomes, _, _ = manifest(args.manifest_b64)
    label_of = {vanilla.HOUSE_NUMBERS[house]: label for house, label in game['sides']}
    slot_of = {vanilla.HOUSE_NUMBERS[house]: slot for slot, (house, _) in enumerate(game['sides'], 1)}
    house_rows = []
    for house in final:
        number = int(house['house'])
        detectors = audit.detect(groups[number])
        last = max(groups[number], key=lambda sample: int(sample['cycle']))
        score = scores[number]
        house_rows.append(dict(meta, **house, **detectors, score=round(score.score, 4), drawn=int(score.drawn),
            won=int(house['alive'] == '1' and sum(entry['alive'] == '1' for entry in final) == 1),
            attack_telemetry=1, attack_count=len(attacks[number]), first_attack_minute=min(attacks[number]) if attacks[number] else '',
            air_kill_telemetry=capture['status'], **{key: last.get(key, '') for key in audit.TROOPER_FIELDS},
            **{key: air_final[number][key] for key in audit.AIR_ATTRIBUTION_FIELDS + audit.AIR_COUNT_FIELDS},
            **{key: last.get(key, '') for key in ('storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost')},
            flags='/'.join(name for name in audit.DETECTORS if detectors[name]), genome_id=label_of[number],
            genome_digest=genome_model.genome_id(genomes[label_of[number]]), slot=slot_of[number],
            comparison_id=game['comparison_id'], mod=game['mod'], research=game['research']))
    tables = {'houses.csv': (HOUSE_FIELDS, house_rows), 'samples.csv': (SAMPLE_FIELDS, samples),
              'waves.csv': (audit.ATTACK_FIELDS, waves),
              'air-kills.csv': (audit.AIR_KILL_FIELDS, [dict(meta, **event) for event in capture['events']])}
    supplemental = audit.tech_audit(lines, samples, final, capture)
    for filename, fields in audit.TECH_TABLES:
        tables[filename] = fields, [dict(meta, mod=game['mod'], **entry) for entry in supplemental[filename]]
    units = {}
    for entry in markers(lines, 'GENOME_UNIT'):
        units[(int(entry['house']), int(entry['cycle']), int(entry['item']))] = dict(meta, **entry)
    tables['units.csv'] = UNIT_FIELDS, [units[key] for key in sorted(units)]
    for filename, marker, fields in (
            ('formation-waves.csv', 'FORMATION_WAVE', vanilla.WAVE_FIELDS),
            ('army.csv', 'FFA_ARMY_SAMPLE', ('house',) + vanilla.ARMY_FIELDS),
            ('mix.csv', 'UNIT_MIX_SAMPLE', ('house',) + vanilla.MIX_FIELDS)):
        tables[filename] = ('id', 'label') + fields, [dict(id=game['id'], label=label_of.get(int(row['house']), ''), **row)
                                                    for row in markers(lines, marker)]
    return tables


def write_table(path, fields, rows):
    with Path(path).open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)


def observations(args, game, row, tables, fixture_path, inputs, results):
    from ai_baseline_scorecard import COLUMNS, scorecard
    snapshot = inputs / 'scorecard'
    snapshot.mkdir()
    write_table(snapshot / 'games.csv', GAME_FIELDS, [row])
    for name, (fields, entries) in tables.items():
        write_table(snapshot / name, fields, entries)
    run = dict(release='genome-' + row['source_commit'][:12], source_commit=row['source_commit'],
               baseline_release_source=row['source_commit'], arm='genome', research=row['research'], mod=game['mod'],
               results=str(snapshot), format='audit', fixtures=str(fixture_path.resolve()), ids=[game['id']])
    labels = {str(vanilla.HOUSE_NUMBERS[house]): label for house, label in game['sides']}
    _, genomes, _, _ = manifest(args.manifest_b64)
    rows = []
    for observation in scorecard(run):
        observation = dict(observation)
        label = labels.get(str(observation['house']))
        if label is None:
            raise ValueError('Canonical scorecard emitted an unassigned house')
        minute = float(observation['minute'])
        phase = 'all' if minute < 0 else PHASES[sum(minute >= boundary for boundary in args.phase_minutes[1:])]
        observation.update(arm=label, game_id=game['id'], genome_id=label, genome_digest=genome_model.genome_id(genomes[label]),
                           comparison_id=game['comparison_id'], format=game['format'], size=game['size'], base=game['base'],
                           spice_level=game['spice_level'], phase=phase, manifest_sha256=game['manifest_sha256'],
                           phase_schedule_sha256=row['phase_schedule_sha256'])
        rows.append(observation)
    if not rows:
        raise ValueError('Canonical scorecard produced no observations')
    path = results / ('observations-' + game['id'] + '.csv.gz')
    with gzip.open(path, 'wt', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=tuple(COLUMNS) + OBSERVATION_EXTRA)
        writer.writeheader()
        writer.writerows(rows)
    return path.name


def play(args, scratch, binary, game):
    results, scratch = Path(args.results), Path(scratch)
    results.mkdir(parents=True, exist_ok=True)
    out, inputs = scratch / game['id'], scratch / (game['id'] + '-inputs')
    receipts = phase_inputs(args, game, inputs)
    started = time.monotonic()
    process = subprocess.run(command(args, binary, game, out, inputs), cwd=root, capture_output=True, text=True,
                             env=dict(os.environ, BALANCE_DUNECITY_AUDIT='1', DUNECITY_TECH_SAVINGS_AUDIT='1'))
    lines = (out / 'run.log').read_text(encoding='utf-8', errors='replace').splitlines() if (out / 'run.log').exists() else []
    summary = json.loads((out / 'summary.json').read_text(encoding='utf-8')) if (out / 'summary.json').exists() else {}
    result = next(iter(markers(lines, 'FORMATION_DUEL_RESULT')), {})
    setup = next(iter(markers(lines, 'GENOME_SETUP')), {})
    generated = next(iter(markers(lines, 'GENOME_MAP')), {})
    timing = next(iter(markers(lines, 'PROBE_TIMING')), {})
    ending = next(iter(markers(lines, 'CAMPAIGN_BALANCE_RESULT')), {})
    final_cycle = int(ending.get('cycle', 0))
    end_s = result.get('end_s', '')
    eliminated = {int(entry['house']): entry['end_s'] for entry in markers(lines, 'VANILLA_MATCH_ELIMINATED')}
    slot_of = {vanilla.HOUSE_NUMBERS[house]: slot for slot, (house, _) in enumerate(game['sides'], 1)}
    final = [dict(entry, team=str(slot_of[int(entry['house'])]), eliminated_s=eliminated.get(int(entry['house']), '-1'), sortie_minutes='')
             for entry in markers(lines, 'FORMATION_DUEL_HOUSE') if int(entry['house']) in slot_of]
    merged = defaultdict(dict)
    for line in lines:
        for marker in ('ELIMINATION_SAMPLE:', 'DUNECITY_AUDIT:', 'DUNECITY_ECONOMY:', 'DUNECITY_TROOPERS:', 'GENOME_STOCK:'):
            if marker in line:
                sample = audit.parse(line, marker)
                sample.update(json.loads(sample.pop('economy', '{}')))
                merged[(int(sample['house']), int(sample['cycle']))].update(sample)
    samples = [dict(metadata(game), **sample) for _, sample in sorted(merged.items())]
    capture = audit.air_kill_capture(out, final, final_cycle, True)
    audit.add_air_kill_samples(samples, capture)
    planned = {(int(entry['house']), entry['phase']): entry for entry in markers(lines, 'GENOME_CONFIG')}
    applied = {(int(entry['house']), entry['phase']): entry for entry in markers(lines, 'GENOME_PHASE')}
    phase_rows, phase_errors = [], []
    for receipt in receipts:
        key = receipt['house'], receipt['phase']
        config, actual = planned.get(key, {}), applied.get(key, {})
        start_cycle = receipt['start_minute'] * audit.CYCLES_PER_MINUTE
        if not config or config.get('genome') != receipt['genome_id'] or int(config.get('start_cycle', -1)) != start_cycle:
            phase_errors.append('missing_phase_config')
        if start_cycle < final_cycle and (not actual or int(actual['cycle']) != start_cycle
                                         or actual['config_hash'] != config.get('config_hash')):
            phase_errors.append('missing_phase_application')
        phase_rows.append(dict(id=game['id'], **{field: receipt[field] for field in ('house', 'genome_id', 'phase', 'start_minute', 'ini_sha256')},
                               start_cycle=start_cycle, applied_cycle=actual.get('cycle', ''), config_hash=config.get('config_hash', ''),
                               applied=int(bool(actual))))
    options_path, map_path = inputs / 'effective-options.ini', inputs / 'map.ini'
    options = configparser.ConfigParser(interpolation=None)
    if options_path.is_file():
        options.read(options_path, encoding='utf-8')
    values = dict(options['Game Options']) if options.has_section('Game Options') else {}
    research = 'on' if values.get('research lab', '').lower() in ('true', '1') else 'off'
    installments = values.get('technology installments', '')
    problems = []
    if process.returncode or not result or len(final) != len(game['sides']):
        problems.append('rc=' + str(process.returncode))
    if not samples or any('power' not in sample for sample in samples):
        problems.append('incomplete_audit')
    if not map_path.is_file() or not options_path.is_file() or not setup:
        problems.append('missing_provenance')
    if research != args.research:
        problems.append('research_context_mismatch')
    problems += sorted(set(phase_errors))
    phase_schedule = dict(schema_version=1, phases=receipts)
    row = dict(metadata(game), sides=','.join(f'{house}:{slot}:{label}' for slot, (house, label) in enumerate(game['sides'], 1)),
               map_seed=generated.get('map_seed', ''), terrain_attempts=generated.get('attempts', ''), skipped=generated.get('skipped', ''),
               outcome='time_limit' if result.get('winner', '-1') == '-1' else 'decided',
               winner_team=slot_of.get(int(result.get('winner', -1)), 0), minutes=final_cycle / audit.CYCLES_PER_MINUTE,
               cycle=final_cycle,
               loop_ms=timing.get('loop_ms', ''), wall_s=round(time.monotonic() - started, 3),
               digest=next((entry['digest'] for entry in markers(lines, 'PROBE_DIGEST') if entry.get('final') == '1'), ''),
               attack_telemetry=1, error=';'.join(problems),
               **{field: setup.get(field, '') for field in ('config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash')},
               ai_config_hashes=','.join(f'{entry["house"]}:{entry["config_hash"]}' for entry in phase_rows if entry['phase'] == 'opening'),
               phase_config_hashes=canonical(phase_rows), probe_sha256=file_digest(binary),
               source_commit=summary.get('sourceCommit', ''), source_fingerprint=summary.get('sourceFingerprint', ''),
               source_fingerprint_error=summary.get('sourceFingerprintError', ''), air_kill_telemetry=capture['status'],
               air_kill_telemetry_reasons=capture['reasons'], air_kill_attribution=capture['attribution'],
               air_kill_unknown_attribution=capture['unknown_attribution'], capture_mib=args.capture_mib,
               **{field: game[field] for field in ('tag', 'mod', 'map', 'map_width', 'map_height', 'context_id', 'comparison_id',
                                                   'manifest_sha256', 'registry_sha256', 'manifest_b64')},
               research=research, technology_installments=installments, phase_schedule_sha256=digest(phase_schedule),
               phase_schedule_json=canonical(phase_schedule), map_sha256=file_digest(map_path) if map_path.is_file() else '',
               options_sha256=file_digest(options_path) if options_path.is_file() else '', options_json=canonical(values),
               requested_options_sha256=file_digest(inputs / 'requested-options.ini'), winner=result.get('winner', ''),
               winner_label=next((label for house, label in game['sides'] if vanilla.HOUSE_NUMBERS[house] == int(result.get('winner', -1))), ''),
               end_s=end_s, observations='', fixtures='',
               hash_scope='effective_QuantBotConfig_getConfigHash_only;manifest_and_phase_schedule_separate;not_multiplayer_protocol')
    # Lean screening keeps fitness rows; errors and a deterministic audit sample keep full traces.
    audit_draw = int(hashlib.sha256(game['id'].encode('utf-8')).hexdigest()[:12], 16)
    full = (args.evidence == 'full' or bool(row['error'])
            or (args.audit_every > 0 and audit_draw % args.audit_every == 0))
    row['evidence'] = row['diagnostics'] = retain_evidence(results, out, inputs, game, full)
    if not row['error']:
        tables = tables_for_game(args, game, lines, summary, final, samples, capture)
        for house in tables['houses.csv'][1]:
            house['technology_installments'] = installments
        fixture_path = results / ('fixtures-' + game['id'] + '.json')
        fixture = dict(schema_version=1, games=[dict(id=game['id'], context_id=game['context_id'], mod=game['mod'],
                       seed=game['seed'], difficulty=game['difficulty'], research=research,
                       map_sha256=row['map_sha256'], options_sha256=row['options_sha256'],
                       roster=[[number, team] for number, team in slot_of.items()],
                       city_effects=values.get('city effects', '').lower() in ('true', '1'),
                       options_hash=row['options_hash'], map_hash=row['map_hash'],
                       summary_file='evidence-' + game['id'] + '-summary.json.gz',
                       map_file='evidence-' + game['id'] + '-input-map.ini.gz',
                       options_file='evidence-' + game['id'] + '-input-effective-options.ini.gz')])
        fixture_path.write_text(canonical(fixture) + '\n', encoding='utf-8')
        row['fixtures'] = fixture_path.name
        row['observations'] = observations(args, game, row, tables, fixture_path, inputs, results)
        for filename, (fields, entries) in tables.items():
            if full or filename in LEAN_TABLES:
                audit.append(results / filename, fields, entries)
    _, genomes, _, _ = manifest(args.manifest_b64)
    assignments = [dict(id=game['id'], context_id=game['context_id'], house=vanilla.HOUSE_NUMBERS[house], slot=slot,
                        genome_id=label, genome_digest=genome_model.genome_id(genomes[label]), comparison_id=game['comparison_id'],
                        **{field: game[field] for field in ('difficulty', 'mod', 'format', 'size', 'base', 'spice_level', 'manifest_sha256')},
                        research=research, technology_installments=installments, phase_schedule_sha256=row['phase_schedule_sha256'])
                   for slot, (house, label) in enumerate(game['sides'], 1)]
    audit.append(results / 'assignments.csv', ASSIGNMENT_FIELDS, assignments)
    audit.append(results / 'config-phases.csv', PHASE_FIELDS, phase_rows)
    audit.append(results / 'games.csv', GAME_FIELDS, [row])
    finish_game(scratch, out, bool(row['error']))
    if not full:
        # The node archives remaining scratch: lean games leave no large traces behind.
        shutil.rmtree(out, ignore_errors=True)
    shutil.rmtree(inputs)
    return row


def report(args):
    results = Path(args.results)
    games = read(results / 'games.csv')
    good = [game for game in games if not game.get('error')]
    print(f'Genome games {len(good)}/{len(games)}; compressed evidence and genuine effective phase hashes retained')
    if args.scores:
        from ai_baseline_scorecard import COLUMNS
        target = Path(args.scores)
        opener = gzip.open if target.suffix == '.gz' else open
        with opener(target, 'wt', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=tuple(COLUMNS) + OBSERVATION_EXTRA)
            writer.writeheader()
            for game in good:
                with gzip.open(results / game['observations'], 'rt', encoding='utf-8', newline='') as source:
                    writer.writerows(csv.DictReader(source))
    return bool(games and len(good) != len(games))


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    done = {row['id'] for row in read(results / 'games.csv') if not row.get('error')
            and row.get('observations') and (results / row['observations']).is_file()}
    todo = [game for game in schedule(args) if game['id'] not in done]
    print(f'{len(todo)} genome games to play ({len(done)} complete)', flush=True)
    if todo:
        require_free_space()
        with scratch_dir('genome-match', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, game) for game in todo]
                for index, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    print(f'[{index}/{len(todo)}] {row["id"]} {row["winner_label"] or "time-limit"} {row["error"]}', flush=True)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--manifest-b64', help='Inline base64 JSON {schema_version:1,genomes:[...]} (required for games)')
parser.add_argument('--results', required=True, type=Path)
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--prebuilt-probe', type=Path)
parser.add_argument('--scratch')
parser.add_argument('--tag', default='genome')
parser.add_argument('--pairs', help='1v1 genome:genome[,genome:genome], including implicit default')
parser.add_argument('--ffa', help='Four-house FFA: one, two balanced, or four IDs; duplicates supported')
parser.add_argument('--formats', nargs='+', choices=('1v1', 'ffa'))
parser.add_argument('--mods', '--mod', nargs='+', type=str.lower, choices=tuple(MODS), default=list(MODS))
parser.add_argument('--difficulties', '--difficulty', nargs='+', choices=('medium', 'hard', 'brutal'), default=['medium', 'hard', 'brutal'])
parser.add_argument('--sizes', '--size', nargs='+', type=int, choices=(64, 96, 128, 160, 192, 256), default=[128])
parser.add_argument('--maps', nargs='+', help='Bundled stems/aliases, or rs<size>-vanilla|dune2r|dunecity')
parser.add_argument('--houses', nargs='+', choices=tuple(vanilla.HOUSE_NUMBERS))
parser.add_argument('--bases', '--base', nargs='+', choices=tuple(BASES), default=['cy', 'fortress'])
parser.add_argument('--army', type=int, choices=range(7), help='Generator budget index; default CY0, Fortress5')
parser.add_argument('--city', type=int, choices=range(6), help='Generator population index; default city Fortress4, otherwise0')
parser.add_argument('--spice', nargs='+', choices=tuple(SPICE), default=['none', 'medium'])
parser.add_argument('--tech', type=int, choices=range(1, 10), help='Initial technology; default research-on1, research-off9')
parser.add_argument('--research', choices=('off', 'on'), default='off')
parser.add_argument('--technology-installments', choices=('off', 'on'), help='Global hashed human-and-AI rule; default mod rules')
parser.add_argument('--research-pace', type=int, choices=range(4), help='Global research pace: Fast0, Normal1, Slow2, VerySlow3')
parser.add_argument('--game-options', type=Path, help='Frozen requested Game Options INI over real mod defaults')
parser.add_argument('--seeds', nargs='+', type=int, help='Fixed unsigned 32-bit seeds (required for games)')
parser.add_argument('--phase-minutes', nargs=3, type=int, default=[0, 10, 20], metavar=('OPENING', 'MID', 'LATE'))
parser.add_argument('--minutes', type=int, default=40)
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--capture-mib', type=int, default=256)
parser.add_argument('--evidence', choices=('lean', 'full'), default='lean',
                    help='lean: fitness rows and small receipts; full: every trace and table')
parser.add_argument('--audit-every', type=int, default=50,
                    help='Lean only: deterministic 1-in-N full-capture audit sample; 0 disables')
parser.add_argument('--jobs', type=int, default=1)
parser.add_argument('--report', action='store_true')
parser.add_argument('--scores', type=Path, help='Merge canonical per-game observations, usually observations.csv.gz')
args = parser.parse_args()
try:
    validate(args)
    sys.exit(report(args) if args.report else run(args))
except ValueError as error:
    parser.error(str(error))
