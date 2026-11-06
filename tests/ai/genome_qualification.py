"""Candidate-specific mandatory AllAgainstAtreides receipts (stdlib, Python 3.9).

These hard wins are not selfplay win_score. Only the exact candidate/source,
actual Brutal-v-allied-Hard roster and every frozen seed can qualify.
"""
import ast
import configparser
import csv
import gzip
import hashlib
import json
from functools import lru_cache
from pathlib import Path
import re
import io
import zlib
import subprocess
import types

import genome_model

ROOT = Path(__file__).resolve().parents[2]
CPM = 3750
NAMES = ('all_against_atreides', 'all_against_atreides_dune2r')
ROSTER = (('harkonnen', 1, 'AH'), ('atreides', 2, 'QB'), ('ordos', 1, 'AH'),
          ('fremen', 1, 'AH'), ('sardaukar', 1, 'AH'))
NUMBERS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4}
FIELDS = ('qualification_suite', 'candidate_genome', 'genome_digest', 'genome_overlay_sha256',
          'effective_options_sha256', 'qualification_status', 'qualification_evidence',
          'qualification_evidence_sha256', 'qualification_cycle')


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


@lru_cache(maxsize=1)
def transport():
    path = Path(__file__).with_name('genome-match.py')
    body = []
    for node in ast.parse(path.read_text(encoding='utf-8'), str(path)).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        body.append(node)
    else:
        raise ValueError('Missing genome-match queue boundary')
    namespace = {'__file__': str(path), '__name__': 'cluster_harness'}
    exec(compile(ast.Module(body=body, type_ignores=[]), str(path), 'exec'), namespace)
    return types.SimpleNamespace(**namespace)


def selected(args):
    encoded, label = getattr(args, 'manifest_b64', None), getattr(args, 'genome', None)
    if not encoded and not label:
        return None
    if not encoded or not label:
        raise ValueError('--manifest-b64 and --genome must be supplied together')
    _, genomes, registry, _ = transport().manifest(encoded)
    if label not in genomes or label == 'default':
        raise ValueError('Qualification needs the actual named candidate, not a default-OFF pass')
    return genomes[label], registry


def options(text):
    ini = configparser.ConfigParser(interpolation=None)
    ini.read_string(text)
    def normalized(value):
        value = value.strip().lower()
        return {'true': '1', 'yes': '1', 'on': '1', 'false': '0', 'no': '0', 'off': '0'}.get(value, value)
    return {key.lower(): normalized(value) for key, value in ini.items('Game Options')}


def _literal(data, name):
    for node in ast.parse(data.decode('utf-8')).body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return tuple(ast.literal_eval(node.value))
    raise ValueError('Missing mandatory seed contract: ' + name)


def native_rule_schema(reader):
    """Inventory the pinned C++ defaults, complete serializer and native FNV field order."""
    header = genome_model._cpp_text(reader('include/DataTypes.h').decode('utf-8'))
    declaration = re.search(r'\bclass\s+GameOptionsClass\s*\{', header)
    if declaration is None:
        raise ValueError('Cannot inventory native game options')
    members = genome_model._block(header, declaration.end())
    declarations = dict(re.findall(r'\b(?:bool|int|TechResearch::Pace)\s+((?!operator\b)\w+)\s*(?:=(?!=)\s*([^;]+))?;', members))
    constructor = re.search(r'GameOptionsClass\(\)\s*:\s*([\s\S]*?)\{', members)
    if constructor is None:
        raise ValueError('Cannot inventory native option constructor')
    defaults = dict(declarations)
    defaults.update(re.findall(r'\b(\w+)\(([^()]*)\)', constructor[1]))
    pace = genome_model._cpp_text(reader('include/TechResearch.h').decode('utf-8'))
    enums = {'TechResearch::Pace::' + key: value for key, value in re.findall(r'\b(\w+)\s*=\s*(\d+)', pace)}
    def scalar(token):
        token = token.strip()
        return {'true': '1', 'false': '0'}.get(token, enums.get(token, token if re.fullmatch(r'-?\d+', token) else None))
    writer = genome_model._function(genome_model._cpp_text(reader('src/globals.cpp').decode('utf-8')),
                                    'writeGameOptionsToConfig')
    bindings = re.findall(r'config\.set(?:Bool|Int)Value\(\s*section\s*,\s*"([^"]+)"\s*,\s*'
                          r'(?:static_cast<int>\(\s*)?options\.(\w+)\s*\)?\s*\)', writer)
    fields = {member: key for key, member in bindings}
    if not bindings or set(fields) != set(declarations) or len(fields) != len(bindings):
        raise ValueError('Native rules need a complete, unique option serializer')
    hash_start = re.search(r'\bgetHash\(\)\s*const\s*\{', members)
    if hash_start is None:
        raise ValueError('Cannot inventory native option hash')
    body = genome_model._block(members, hash_start.end())
    parts = []
    for expression in re.findall(r'optStr\s*\+=\s*([^;]+);', body):
        prefix = re.match(r'("(?:[^"\\]|\\.)*")\s*\+\s*', expression)
        text = json.loads(prefix[1]) if prefix else ''
        remainder = expression[prefix.end():] if prefix else expression
        field = re.fullmatch(r'std::to_string\((?:static_cast<std::uint32_t>\()?(\w+)\)?\)', remainder.strip())
        if field is None or field[1] not in fields:
            raise ValueError('Unsupported native option hash expression')
        parts.append((text, fields[field[1]].lower()))
    offset = re.search(r'uint64_t\s+hash\s*=\s*(\d+)ULL', body)
    prime = re.search(r'uint64_t\s+prime\s*=\s*(\d+)ULL', body)
    if (offset is None or prime is None
            or {key for _, key in parts} != {key.lower() for key, _ in bindings} - {'immortal human player'}):
        raise ValueError('Cannot certify complete native option hash coverage')
    return dict(keys=[key for key, _ in bindings],
                defaults={key.lower(): scalar(defaults[member]) for key, member in bindings},
                hash_parts=parts, offset=int(offset[1]), prime=int(prime[1]))


def native_options_hash(values, schema):
    if set(values) != {key.lower() for key in schema['keys']}:
        raise ValueError('Incomplete native effective options')
    payload = ''.join(prefix + str(int(values[key])) for prefix, key in schema['hash_parts']).encode()
    value = schema['offset']
    for byte in payload:
        value = ((value ^ byte) * schema['prime']) & ((1 << 64) - 1)
    return '%016x' % value


def contracts(reader=None, source_fingerprint=None):
    reader = reader or (lambda name: (ROOT / name).read_bytes())
    from source_provenance import SOURCE_FINGERPRINT_VERSION
    schema = native_rule_schema(reader)
    vanilla = _literal(reader('tests/ai/run-all-against-atreides.py'), 'DEFAULT_SEEDS')
    matrix = _literal(reader('tests/ai/scenario-matrix.py'), 'AAA_SEEDS')
    if len(vanilla) != 6 or matrix[:6] != vanilla or len(matrix) != 7:
        raise ValueError('Mandatory AllAgainst contract must retain the original six and saved seventh seeds')
    map_bytes = reader('data/maps/singleplayer/5P - 128x128 - All against Atreides.ini')
    out = {}
    for name, cell, mod, seeds, limit, file in (
            (NAMES[0], 'qb-v-4ah/vanilla', 'vanilla', vanilla, 41, 'all-against-atreides-options.ini'),
            (NAMES[1], 'qb-v-4ah-saved/Dune2R', 'Dune2R', matrix, 40, 'all-against-atreides-dune2r-options.ini')):
        path = 'tests/ai/' + file
        data = reader(path)
        out[name] = dict(name=name, cell=cell, mod=mod, seeds=list(seeds), limit=limit,
                         map_sha256=sha(map_bytes), options_sha256=sha(data), options=options(data.decode('utf-8')),
                         options_path=path, options_ini=data.decode('utf-8'), rule_schema=schema,
                         source_fingerprint=source_fingerprint, source_fingerprint_version=SOURCE_FINGERPRINT_VERSION)
    return out


@lru_cache(maxsize=8)
def pinned_contracts(source):
    if not re.fullmatch(r'[0-9a-f]{40}', source):
        raise ValueError('Qualification needs the exact full source SHA')
    from source_provenance import pinned_source_fingerprint
    def reader(name):
        return subprocess.check_output(['git', 'show', source + ':' + name], cwd=str(ROOT))
    return (contracts(reader, pinned_source_fingerprint(ROOT, source)),
            genome_model._registry(json.loads(reader('tests/ai/genome-registry.json'))))


def validate_schedule(args, games):
    candidate = selected(args)
    if candidate is None:
        if getattr(args, 'qualification_smoke', False):
            raise ValueError('--qualification-smoke needs an actual candidate manifest/genome')
        return
    if not re.fullmatch(r'[0-9a-f]{40}', getattr(args, 'qualification_source', '') or ''):
        raise ValueError('Candidate qualification requires --qualification-source FULL_COMMIT_SHA')
    if (args.tier != 'full' or not args.check or args.seeds_per_cell is not None
            or getattr(args, 'aaa_seeds', None) is not None or args.shard
            or getattr(args, 'minutes', None) is not None or args.style or args.game_options):
        raise ValueError('Candidate qualification preserves full seeds, roster, options and deadline; use --tier full --check')
    expected = contracts()
    by_cell = {value['cell']: value for value in expected.values()}
    seen = {}
    for game in games:
        contract = by_cell.get(game['cell'])
        if (contract is None or game['category'] != 'aaa' or tuple(game['sides']) != ROSTER
                or game['expect'] != ('wins', 2, 40) or game['minutes'] != contract['limit']):
            raise ValueError('Candidate qualification is restricted to the unchanged mandatory AllAgainst cells')
        seen.setdefault(game['cell'], []).append(game['seed'])
    if not seen or any(sorted(seeds) != sorted(by_cell[cell]['seeds']) for cell, seeds in seen.items()):
        raise ValueError('Candidate qualification requires every original seed exactly once')


def context(contract):
    return dict(mod=contract['mod'].lower(), difficulty='brutal', house='atreides', format='team', size=128)


def phase_payloads(item, contract, phase_minutes, registry):
    lines = ['[Schedule]', 'elapsed=false']
    lines += ['%s=%d' % pair for pair in zip(genome_model.PHASES, phase_minutes)]
    lines += ['', '[Genomes]', '1=' + item['id']]
    payload = {'schedule.ini': '\n'.join(lines) + '\n'}
    for phase in genome_model.PHASES:
        payload['1-' + phase + '.ini'] = genome_model.render_ini(item, dict(context(contract), phase=phase), registry)
    return payload


def overlay_hash(payload):
    digest = hashlib.sha256()
    for name, text in sorted(payload.items()):
        digest.update(name.encode() + b'\0' + text.encode() + b'\0')
    return digest.hexdigest()


def requested_ini(item, contract, registry):
    """Freeze every native rule; only declared game genes may differ from its compiled default."""
    values = dict(contract['rule_schema']['defaults'])
    values.update(contract['options'])
    rules = genome_model.game_rules([item], registry)
    for gene in registry:
        if not genome_model.is_game_rule(gene):
            continue
        level = str(rules[gene['id']])
        frozen = contract['options'].get(gene['key'].lower())
        if frozen is not None and frozen != level:
            raise ValueError('Candidate conflicts with frozen AllAgainst options; qualification is UNVERIFIED')
        values[gene['key'].lower()] = level
    if any(value is None for value in values.values()):
        raise ValueError('Unresolved native game option default')
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.add_section('Game Options')
    for key in contract['rule_schema']['keys']:
        ini.set('Game Options', key, values[key.lower()])
    stream = io.StringIO()
    ini.write(stream)
    return stream.getvalue()


def prepare(args, game, directory):
    item, registry = selected(args)
    expected, _ = pinned_contracts(args.qualification_source)
    contract = next(value for value in expected.values() if value['cell'] == game['cell'])
    requested = requested_ini(item, contract, registry)
    transport().render_phase_inputs([(1, item['id'])], {key: value for key, value in context(contract).items()
                                                     if key not in ('house',)}, {item['id']: item}, directory,
                                    args.phase_minutes, registry)
    (directory / 'game-options.ini').write_text(requested, encoding='utf-8')
    return directory


def _markers(lines, marker):
    return [dict(part.split('=', 1) for part in line.split(marker + ':', 1)[1].split() if '=' in part)
            for line in lines if marker + ':' in line]


def _hex(value, lengths=(16, 64)):
    return isinstance(value, str) and len(value) in lengths and re.fullmatch(r'[0-9a-fA-F]+', value) is not None


def native_verdict(proof, item, source, contract, registry, phase_minutes):
    """Missing/stale/wrong evidence never borrows another candidate's win."""
    try:
        if (len(phase_minutes) != 3 or phase_minutes[0] != 0
                or not 0 < phase_minutes[1] < phase_minutes[2] <= 10000):
            return 'unverified'
        result, setup = proof['result'], proof['setup']
        if result.get('mode') != 'match' or result.get('outcome') not in ('won', 'lost', 'time_limit'):
            return 'unverified'
        identity = genome_model.genome_id(item)
        expected = phase_payloads(item, contract, phase_minutes, registry)
        requested = requested_ini(item, contract, registry)
        if (proof['source_commit'] != source or proof['genome_digest'] != identity
                or proof['genome'] != item or proof['suite'] != contract['name']
                or proof['seed'] not in contract['seeds'] or proof['phase_minutes'] != list(phase_minutes)
                or proof['requested_map_sha256'] != contract['map_sha256']
                or proof['frozen_options_sha256'] != contract['options_sha256']
                or proof['requested_options_ini'] != requested
                or proof['requested_options_sha256'] != sha(requested.encode())
                or proof.get('working_tree_modified') is not False
                or proof.get('source_fingerprint_error') != ''
                or not _hex(contract.get('source_fingerprint'), (64,))
                or proof.get('source_fingerprint') != contract['source_fingerprint']
                or proof.get('source_fingerprint_version') != contract['source_fingerprint_version']
                or proof['effective_map_sha256'] != contract['map_sha256']
                or proof['phase_inputs'] != expected or proof['overlay_sha256'] != overlay_hash(expected)
                or not _hex(proof['probe_sha256'], (64,)) or proof.get('error')
                or setup.get('observing') != '1' or setup.get('city_sim') != '0'
                or setup.get('style') not in ('', 'default') or int(setup['seed']) != proof['seed']
                or not _hex(setup.get('options_hash'))):
            return 'unverified'
        effective = proof['effective_options']
        if (effective != options(requested)
                or sha(canonical(effective).encode()) != proof['effective_options_sha256']
                or setup['options_hash'] != native_options_hash(effective, contract['rule_schema'])):
            return 'unverified'
        roster = sorted((int(row['house']), int(row['team']), row['players']) for row in proof['roster'])
        expected_roster = sorted((NUMBERS[house], team, 'qBotBrutal' if code == 'QB' else 'AIPlayerHard')
                                 for house, team, code in ROSTER)
        if roster != expected_roster:
            return 'unverified'
        cycle = int(result['cycle'])
        if cycle <= 0 or int(setup.get('cycle', -1)) != 0:
            return 'unverified'
        configured = {row['phase']: row for row in proof['configured'] if row['house'] == '1'}
        applied = {row['phase']: row for row in proof['applied'] if row['house'] == '1'}
        reached = {phase for phase, minute in zip(genome_model.PHASES, phase_minutes) if minute * CPM < cycle}
        if len(proof['applied']) != len(reached) or set(applied) != reached:
            return 'unverified'
        if len(proof['configured']) != 3 or set(configured) != set(genome_model.PHASES):
            return 'unverified'
        brutal_difficulty = str(transport().DIFFICULTIES.index('brutal'))
        for phase, minute in zip(genome_model.PHASES, phase_minutes):
            config = configured[phase]
            if (config['genome'] != item['id'] or int(config['start_cycle']) != minute * CPM
                    or config.get('difficulty') != brutal_difficulty
                    or config.get('controller') != '0' or not _hex(config['config_hash'])):
                return 'unverified'
            if minute * CPM < cycle:
                live = applied.get(phase, {})
                if (live.get('genome') != item['id'] or int(live.get('cycle', -1)) != minute * CPM
                        or live.get('difficulty') != brutal_difficulty
                        or live.get('controller') != '0' or live.get('config_hash') != config['config_hash']):
                    return 'unverified'
        if proof['candidate_config_hash'] != configured['opening']['config_hash']:
            return 'unverified'
        if result.get('winner_team') != '2' or result.get('outcome') == 'time_limit' or cycle > 40 * CPM:
            return 'infeasible'
        houses = {int(row['house']): row for row in proof['houses']}
        if (len(proof['houses']) != 5 or set(houses) != set(NUMBERS.values()) or houses[1]['alive'] != '1'
                or any(houses[number]['alive'] != '0' for number in (0, 2, 3, 4))):
            return 'unverified'
        return 'feasible'
    except (KeyError, TypeError, ValueError):
        return 'unverified'


def capture(args, game, directory, lines, result, houses, binary, row, summary):
    item, registry = selected(args)
    expected, _ = pinned_contracts(args.qualification_source)
    contract = next(value for value in expected.values() if value['cell'] == game['cell'])
    phase_inputs = {path.name: path.read_text(encoding='utf-8') for path in directory.glob('*.ini')
                    if path.name == 'schedule.ini' or path.name.startswith('1-')}
    effective_path, map_path = directory / 'effective-options.ini', directory / 'map.ini'
    effective = options(effective_path.read_text()) if effective_path.is_file() else {}
    configs = _markers(lines, 'SCENARIO_AI_CONFIG')
    setup = _markers(lines, 'SCENARIO_SETUP')
    requested = (directory / 'game-options.ini').read_text(encoding='utf-8')
    proof = dict(suite=contract['name'], source_commit=row['source_commit'], genome=item,
                 genome_digest=genome_model.genome_id(item), seed=game['seed'], phase_minutes=list(args.phase_minutes),
                 requested_map_sha256=contract['map_sha256'], frozen_options_sha256=contract['options_sha256'],
                 requested_options_ini=requested, requested_options_sha256=sha(requested.encode()),
                 source_fingerprint=row.get('source_fingerprint', ''),
                 source_fingerprint_error=summary.get('sourceFingerprintError'),
                 source_fingerprint_version=contract['source_fingerprint_version'],
                 working_tree_modified=summary.get('workingTreeModified'),
                 effective_map_sha256=sha(map_path.read_bytes()) if map_path.is_file() else '',
                 effective_options=effective, effective_options_sha256=sha(canonical(effective).encode()),
                 phase_inputs=phase_inputs, overlay_sha256=overlay_hash(phase_inputs),
                 probe_sha256=sha(Path(binary).read_bytes()), result=result, houses=houses,
                 setup=setup[0] if len(setup) == 1 else {}, roster=_markers(lines, 'SCENARIO_ROSTER'),
                 configured=_markers(lines, 'GENOME_CONFIG'), applied=_markers(lines, 'GENOME_PHASE'),
                 candidate_config_hash=next((entry['config_hash'] for entry in configs if entry['house'] == '1'), ''),
                 error=row['error'])
    status = native_verdict(proof, item, args.qualification_source, contract, registry, args.phase_minutes)
    name = 'candidate-proof-' + sha(game['id'].encode())[:24] + '.json.gz'
    path = Path(args.results) / name
    with gzip.open(path, 'wt', encoding='utf-8') as stream:
        json.dump(proof, stream, sort_keys=True)
    return dict(qualification_suite=contract['name'], candidate_genome=item['id'], genome_digest=proof['genome_digest'],
                genome_overlay_sha256=proof['overlay_sha256'], effective_options_sha256=proof['effective_options_sha256'],
                qualification_status=status, qualification_evidence=name, qualification_evidence_sha256=sha(path.read_bytes()),
                qualification_cycle=result.get('cycle', ''))


def aggregate(states):
    return ('infeasible' if 'infeasible' in states else 'feasible'
            if states and all(state == 'feasible' for state in states) else 'unverified')


def verdicts(paths, source, genomes, phase_minutes=(0, 10, 20)):
    known = {genome_model.genome_id(item): item for item in genomes}
    by_genome = {identity: {name: 'unverified' for name in NAMES} for identity in known}
    if not paths:
        return {identity: dict(suites=states, status='unverified') for identity, states in by_genome.items()}
    try:
        expected, registry = pinned_contracts(source)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        return {identity: dict(suites=states, status='unverified', certification_error=str(error))
                for identity, states in by_genome.items()}
    rows = {}
    for supplied in paths:
        path = Path(supplied)
        path = path / 'games.csv' if path.is_dir() else path
        if not path.is_file():
            continue
        opener = gzip.open if path.suffix == '.gz' else open
        with opener(path, 'rt', encoding='utf-8', newline='') as stream:
            for row in csv.DictReader(stream):
                identity, suite = row.get('genome_digest'), row.get('qualification_suite')
                if identity not in known or suite not in expected or row.get('source_commit') != source:
                    continue
                seed = None
                try:
                    seed = int(row['seed'])
                    evidence = row['qualification_evidence']
                    if Path(evidence).name != evidence:
                        raise ValueError('Unsafe evidence path')
                    proof_path = path.parent / evidence
                    data = proof_path.read_bytes()
                    if sha(data) != row.get('qualification_evidence_sha256'):
                        raise ValueError('Evidence receipt changed')
                    proof = json.loads(gzip.decompress(data))
                    status = native_verdict(proof, known[identity], source, expected[suite], registry, phase_minutes)
                    if (proof['seed'] != seed or row.get('candidate_genome') != known[identity]['id']
                            or row.get('genome_overlay_sha256') != proof['overlay_sha256']
                            or row.get('effective_options_sha256') != proof['effective_options_sha256']):
                        status = 'unverified'
                except (KeyError, ValueError, TypeError, OSError, EOFError, zlib.error):
                    status = 'unverified'
                rows.setdefault((identity, suite, seed), []).append(status)
    result = {}
    for identity in known:
        suites = {name: aggregate([aggregate(rows.get((identity, name, seed), [])) for seed in contract['seeds']]
                                 + rows.get((identity, name, None), []))
                  for name, contract in expected.items()}
        result[identity] = dict(suites=suites, status=aggregate(list(suites.values())))
    return result


def check_run(args):
    item, _ = selected(args)
    result = verdicts([args.results], args.qualification_source, [item], args.phase_minutes)
    report = dict(source_commit=args.qualification_source, genome=item, phase_minutes=list(args.phase_minutes),
                  qualification=result, diagnostic_smoke=bool(getattr(args, 'qualification_smoke', False)),
                  promotion='NOT_AUTHORIZED: held-out strength, history and behaviour checks still required')
    (Path(args.results) / 'candidate-qualification.json').write_text(json.dumps(report, indent=2) + '\n')
    return not getattr(args, 'qualification_smoke', False) and result[genome_model.genome_id(item)]['status'] == 'feasible'
