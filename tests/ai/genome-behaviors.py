#!/usr/bin/env python3
"""Private reported-save feasibility, never a tournament strength score.

Freeze original bytes with --freeze/--case/--output, then transport that INI with
--game-options. The queue discovers schedule/build_probe/play and the top-level
argument boundary. No worker reads a submitter-only genome or mutable save path.
"""
import argparse
import ast
import base64
import configparser
import csv
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import types
from collections import defaultdict
from functools import lru_cache
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import finish_game, probe_binary, require_free_space, restore_fixture_revisions, scratch_dir
import genome_model


def helpers(filename):
    path = Path(__file__).with_name(filename)
    body = []
    for node in ast.parse(path.read_text(encoding='utf-8'), str(path)).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        body.append(node)
    else:
        raise RuntimeError('Missing queue argument boundary: ' + str(path))
    namespace = {'__file__': str(path), '__name__': 'cluster_harness'}
    exec(compile(ast.Module(body=body, type_ignores=[]), str(path), 'exec'), namespace)
    return types.SimpleNamespace(**namespace)


match = helpers('genome-match.py')
KEY = ('case', 'genome')
FIELDS = KEY + ('genome_digest', 'save_sha256', 'input_sha256', 'source_commit', 'source_fingerprint',
                'probe_sha256', 'registry_sha256', 'checks_sha256', 'technology_installments',
                'completed', 'telemetry', 'feasibility', 'wall_s', 'error', 'checks_file', 'evidence_file')
CHECK_FIELDS = KEY + ('genome_digest', 'source_commit', 'house', 'check', 'status', 'reason', 'details')
RESIDENTIAL, MCV, LIGHT_FACTORY, ROCKET = 20, 34, 7, 12
R_EVENTS = ('zone_evaluation', 'capital_plan', 'production_order', 'drain_budget_hold')
EVENTS = R_EVENTS + ('city_generator_choice', 'capture_limit', 'session_start', 'session_end')
CYCLES_PER_MINUTE = 3750


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def checksum(data):
    return hashlib.sha256(data).hexdigest()


@lru_cache(maxsize=1)
def definitions():
    value = json.loads(Path(__file__).with_name('genome-behavior-checks.json').read_text(encoding='utf-8'))
    if value.get('schema_version') != 1:
        raise ValueError('Unsupported behaviour-check schema')
    cases = {case['id']: case for case in value['cases']}
    if len(cases) != len(value['cases']):
        raise ValueError('Duplicate reported issue')
    return cases


@lru_cache(maxsize=8)
def manifest(encoded):
    registry = genome_model.load_registry()
    if not encoded:
        return {'default': genome_model.default_genome(registry)}
    raw = base64.b64decode(encoded + '=' * (-len(encoded) % 4), altchars=b'-_', validate=True)
    value = json.loads(raw.decode('utf-8'), object_pairs_hook=match.unique_object)
    if (not isinstance(value, dict) or set(value) != {'schema_version', 'genomes'}
            or value['schema_version'] != 1 or not isinstance(value['genomes'], list) or not value['genomes']):
        raise ValueError('Manifest must be {schema_version:1,genomes:[...]}')
    result = {}
    for item in value['genomes']:
        genome_model.validate_genome(item, registry)
        if item['id'] in result:
            raise ValueError('Duplicate genome label')
        result[item['id']] = item
    return result


def read(path):
    path = Path(path)
    if not path.exists():
        path = path.with_name(path.name + '.gz')
    if not path.exists():
        return []
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt', newline='', encoding='utf-8') as stream:
        return list(csv.DictReader(stream))


def append(path, fields, rows):
    path = Path(path)
    exists = path.exists()
    with path.open('a', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        if not exists:
            writer.writeheader()
        writer.writerows(rows)


def freeze(save, case_id, output):
    case = definitions()[case_id]
    if not case['adapted']:
        raise ValueError('Reported issue has not been adapted')
    data = Path(save).read_bytes()
    digest = checksum(data)
    if digest != case['save_sha256']:
        raise ValueError('Not the registered original private save')
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config['Research Lab Save'] = {'SHA256': digest,
        'GzipBase64': base64.b64encode(gzip.compress(data, mtime=0)).decode('ascii')}
    config['Genome Behavior'] = {'Case': case_id, 'Context': canonical(case['context'])}
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w', encoding='utf-8', newline='\n') as stream:
        config.write(stream)
    return dict(case=case_id, save_sha256=digest, input_sha256=checksum(output.read_bytes()), input=str(output.resolve()))


def frozen_input(args):
    if not args.game_options:
        raise ValueError('Playing requires frozen --game-options input')
    config = configparser.ConfigParser(interpolation=None)
    if not config.read(args.game_options, encoding='utf-8'):
        raise ValueError('Cannot read frozen behaviour input')
    case_id = config.get('Genome Behavior', 'Case')
    if args.case and args.case != case_id:
        raise ValueError('Scheduled and frozen cases disagree')
    case = definitions()[case_id]
    if not case['adapted']:
        raise ValueError('Reported issue has not been adapted')
    packed = config['Research Lab Save']
    data = gzip.decompress(base64.b64decode(packed['GzipBase64'], validate=True))
    digest = checksum(data)
    if digest != packed['SHA256'] or digest != case['save_sha256']:
        raise ValueError('Private save does not match the frozen/registered original')
    context = json.loads(config.get('Genome Behavior', 'Context'))
    if context != case['context']:
        raise ValueError('Frozen context differs from the registered save context')
    return case_id, data, digest, context


def schedule(args):
    if args.case:
        case_id = args.case
    elif args.game_options:
        config = configparser.ConfigParser(interpolation=None)
        config.read(args.game_options, encoding='utf-8')
        case_id = config.get('Genome Behavior', 'Case')
    else:
        raise ValueError('Give --case and frozen --game-options')
    if not definitions()[case_id]['adapted']:
        raise ValueError('Reported issue has no adapter; its feasibility remains unverified')
    if args.phase_minutes[0] != 0 or not 0 < args.phase_minutes[1] < args.phase_minutes[2] <= 10000:
        raise ValueError('Phase starts must be 0 MID LATE, increasing game minutes')
    if args.capture_mib < 1024 or args.capture_mib > 4096 or args.wall_timeout < 3600:
        raise ValueError('Use 1024-4096 MiB capture and at least 3600 s wall timeout')
    return [(case_id, label) for label in manifest(args.manifest_b64)]


def build_probe(args, scratch):
    out = Path(scratch) / 'probe'
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
                    '--output-dir', str(out), '--build-only'], cwd=root, check=True, capture_output=True, text=True)
    return probe_binary(out)


def optional_winner(winner):
    return bool(winner) and ((winner.get('kind') == 'construction' and winner.get('item') == MCV)
        or (winner.get('kind') == 'trooper_support' and winner.get('reason') == 'first_wor'))


def check_r_admission(events, start_what_makes_sense, houses=None, min_opportunities=10):
    """CityZoning's 2026-11-06 rule, ported verbatim except input is an iterable.

    start_what_makes_sense may also be a (house, cycle) function for genome phases.
    Count positive, selected/legal R plots once per cycle, not per controller.
    Legacy counts only an affordable R capital candidate. Reaching the real
    production/funding gate is enough; a hold may lawfully lose to a higher drain.
    """
    state = {}
    for row in events:
        name, house = row.get('event'), row.get('house')
        if name not in R_EVENTS or (houses is not None and house not in houses):
            continue
        hs = state.setdefault((str(row.get('session')), house),
            {'pending': {}, 'opportunities': {}, 'reached': [], 'accepted': {}})
        data, cycle = row.get('data') or {}, row['cycle']
        if name == 'zone_evaluation':
            demand = ((data.get('candidates') or {}).get(str(RESIDENTIAL)) or {}).get('demand', 0)
            if (data.get('selected') == RESIDENTIAL and data.get('result') == 'selected' and demand > 0
                    and cycle not in hs['opportunities']):
                hs['pending'][cycle] = demand
        elif name == 'capital_plan':
            candidates = data.get('candidates') or {}
            values = candidates.values() if isinstance(candidates, dict) else candidates
            affordable = any(c.get('item') == RESIDENTIAL and c.get('affordable') == 1 for c in values)
            winner = candidates.get(str(data.get('selected'))) if isinstance(candidates, dict) else None
            for opportunity in [c for c in hs['pending'] if c <= cycle]:
                hs['opportunities'][opportunity] = {'demand': hs['pending'].pop(opportunity),
                                                   'r_affordable': affordable, 'winner': winner}
        elif name == 'production_order':
            if data.get('item') == RESIDENTIAL:
                hs['reached'].append([cycle, name, data.get('accepted')])
            if data.get('accepted') == 1:
                hs['accepted'].setdefault(data.get('item'), []).append(cycle)
        elif data.get('item') == RESIDENTIAL:
            hs['reached'].append([cycle, name, data.get('priority')])
    result = {}
    for (session, house), hs in sorted(state.items(), key=lambda item: (item[0][0], str(item[0][1]))):
        for cycle, demand in hs['pending'].items():
            hs['opportunities'][cycle] = {'demand': demand, 'r_affordable': False, 'winner': None}
        opportunities = hs['opportunities']
        counted = sorted(c for c, o in opportunities.items() if
            (start_what_makes_sense(house, c) if callable(start_what_makes_sense) else start_what_makes_sense)
            or o['r_affordable'])
        reached, optional, optional_executed = [], [], False
        if counted:
            first, last = counted[0], counted[-1]
            reached = [r for r in hs['reached'] if r[0] >= first]
            optional = [c for c in counted if optional_winner(opportunities[c]['winner'])]
            items = {opportunities[c]['winner'].get('item') for c in optional}
            optional_executed = any(first <= c <= last for item in items for c in hs['accepted'].get(item, []))
        failures = []
        if len(counted) >= min_opportunities:
            if not reached:
                failures.append('r_never_admitted')
            if len(optional) >= min_opportunities and not optional_executed:
                failures.append('optional_capital_owns_lane')
        winners = {}
        for c in counted:
            winner = opportunities[c]['winner'] or {}
            label = '%s/%s/%s' % (winner.get('kind'), winner.get('reason'), winner.get('item'))
            winners[label] = winners.get(label, 0) + 1
        result['%s/%s' % (session, house)] = {
            'verdict': 'fail' if failures else ('pass' if reached else 'not_applicable'),
            'failures': failures, 'opportunities': len(opportunities), 'counted_opportunities': len(counted),
            'first_cycle': counted[0] if counted else None, 'last_cycle': counted[-1] if counted else None,
            'demand_range': [min(opportunities[c]['demand'] for c in counted),
                             max(opportunities[c]['demand'] for c in counted)] if counted else None,
            'r_affordable_opportunities': sum(1 for o in opportunities.values() if o['r_affordable']),
            'reached': reached, 'optional_winner_opportunities': len(optional),
            'optional_winner_executed': optional_executed, 'winners': winners}
    return result


def evaluate_check(check, samples, events, setup, residential):
    first = samples[0]
    kind = check['type']
    orders = [e for e in events if e['event'] == 'production_order' and e['data'].get('accepted') == 1]
    if kind == 'light_factory_order':
        if first['light_factory'] or not first['yard'] or not first['light_factory_sites']:
            return 'unverified', 'original_lf_opportunity_mismatch', {}
        loss = next((s['cycle'] for s in samples if s['yard'] < first['yard']), float('inf'))
        accepted = [e['cycle'] for e in orders if e['data'].get('item') == LIGHT_FACTORY and e['cycle'] < loss]
        return ('pass' if accepted else 'fail'), 'accepted_order_before_initial_yard_loss', {
            'order_cycles': accepted, 'initial_yard_loss': loss if loss != float('inf') else None,
            'legal_sites': first['light_factory_sites'], 'controllers': first['bots']}
    if kind == 'research_progress':
        progressed = any(s['tech'] > first['tech'] or
            (s['research_target'] > first['tech'] and s['research_target'] != first['research_target']) or
            (first['research_target'] and s['research_target'] == first['research_target']
             and s['research_remaining'] < first['research_remaining']) for s in samples[1:])
        legal = any(s['research_start_legal'] for s in samples)
        return ('pass' if progressed else 'fail' if legal else 'not_applicable'), 'legal_start_or_real_research_progress', {
            'initial_tech': first['tech'], 'highest_tech': max(s['tech'] for s in samples),
            'legal_start_observed': legal, 'progressed': progressed}
    if kind == 'residential_admission':
        rows = list(residential.values())
        statuses = [r['verdict'] for r in rows]
        return ('fail' if 'fail' in statuses else 'pass' if 'pass' in statuses else 'not_applicable'), 'cityzoning_funding_admission', residential
    if kind == 'nuclear_choice':
        if setup.get('concrete_required') != 0:
            return 'unverified', 'founded_nuclear_quote_not_observed', {}
        choices = [e['data'] for e in events if e['event'] == 'city_generator_choice']
        required = ('nuclear', 'nuclear_available', 'nuclear_site', 'cash', 'nuclear_price')
        if any(any(k not in c for k in required) for c in choices):
            return 'unverified', 'nuclear_choice_fields_missing', {}
        funded = [c for c in choices if c['nuclear_available'] and c['nuclear_site'] and c['cash'] >= c['nuclear_price']]
        rejected = sum(not c['nuclear'] for c in funded)
        return ('fail' if rejected else 'pass' if funded else 'not_applicable'), 'funded_legal_nuclear_choices', {
            'funded_opportunities': len(funded), 'funded_rejections': rejected}
    if kind == 'lab_built':
        if first['research_lab']:
            return 'unverified', 'original_missing_lab_mismatch', {}
        built = max(s['research_lab'] for s in samples) > 0
        return ('pass' if built else 'fail'), 'physical_lab_completion', {'lab_observed': built}
    if kind == 'level_two_research':
        started = any(s['research_target'] == 2 for s in samples[1:])
        return ('pass' if started else 'fail'), 'level_two_research_target_observed', {'started': started}
    if kind == 'rocket_cover':
        accepted = [e['cycle'] for e in orders if e['data'].get('item') == ROCKET]
        built = max(s['rocket_turret_built'] for s in samples) > first['rocket_turret_built']
        return ('pass' if accepted or built else 'fail'), 'blue_house_orders_or_builds_rocket', {
            'house': first['house'], 'colour_slot': first['colour_slot'], 'order_cycles': accepted, 'built': built}
    raise ValueError('Unknown behaviour assertion: ' + kind)


def case_feasibility(rows):
    states = [row['status'] for row in rows]
    if 'unverified' in states or not states or all(s == 'not_applicable' for s in states):
        return 'unverified'
    return 'infeasible' if 'fail' in states else 'feasible'


def evaluate_case(case, samples, events, setup, complete, applied, telemetry='complete'):
    by_house = defaultdict(list)
    for sample in samples:
        by_house[int(sample['house'])].append(sample)
    initial = {h: next((s for s in rows if s['phase'] == 'loaded'), {}) for h, rows in by_house.items()}
    houses = case['houses'] or sorted(h for h, s in initial.items() if s.get('bots') and not s.get('other_controllers'))
    if 'colour_slot' in case:
        houses = [h for h, s in initial.items() if s.get('colour_slot') == case['colour_slot'] and s.get('bots')]
        if len(houses) != 1:
            return [dict(house='', check='blue_house_identity', status='unverified', reason='blue_AI_house_not_unique', details='{}')], 'unverified'
    rows = []
    for house in houses:
        observations = sorted(by_house.get(house, []), key=lambda s: s['cycle'])
        own_events = [e for e in events if e.get('house') == house and observations
                      and observations[0]['cycle'] <= e['cycle'] <= observations[-1]['cycle']]
        def swms(h, cycle):
            return next(s['start_what_makes_sense'] for s in reversed(observations) if s['cycle'] <= cycle)
        residential = check_r_admission(own_events, swms, [house])
        for check in case['checks']:
            if check.get('houses') and house not in check['houses']:
                continue
            reason = ('incomplete_or_capped_evidence' if not complete or telemetry != 'complete' else
                      'candidate_not_applied_to_original_controllers' if house not in applied else
                      'missing_loaded_or_finished_snapshot' if not initial.get(house) or
                      not any(s['phase'] == 'finished' for s in observations) else
                      'shared_non_quantbot_controller' if initial[house]['other_controllers'] else '')
            if reason:
                status, detail = 'unverified', {}
            else:
                try:
                    status, reason, detail = evaluate_check(check, observations, own_events, setup, residential)
                except (KeyError, StopIteration, TypeError):
                    status, reason, detail = 'unverified', 'required_measurement_missing', {}
            rows.append(dict(house=house, check=check['id'], status=status, reason=reason, details=canonical(detail)))
    return rows, case_feasibility(rows)


def project_event(row):
    """Keep just the rule's inputs; all other diagnostics are discarded in scratch."""
    name, data = row['event'], row.get('data') or {}
    if name not in EVENTS:
        return None
    fields = {'production_order': ('item', 'accepted'), 'drain_budget_hold': ('item', 'priority'),
              'city_generator_choice': ('nuclear', 'nuclear_available', 'nuclear_site', 'cash', 'nuclear_price')}
    if name in fields:
        data = {k: data[k] for k in fields[name] if k in data}
    elif name == 'zone_evaluation':
        data = {k: data[k] for k in ('selected', 'result') if k in data}
        data['candidates'] = {str(RESIDENTIAL): (row['data'].get('candidates') or {}).get(str(RESIDENTIAL), {})}
    elif name == 'capital_plan':
        candidates = data.get('candidates') or {}
        if not isinstance(candidates, dict):
            raise ValueError('Unsupported capital-plan candidate schema')
        data = {'selected': data.get('selected'), 'candidates': {str(k): {f: c[f] for f in
                ('item', 'affordable', 'kind', 'reason') if f in c} for k, c in candidates.items()}}
    return {**{k: row.get(k) for k in ('session', 'cycle', 'house', 'player', 'seq')}, 'event': name, 'data': data}


def capture_events(run, target):
    events, sessions, ends, capped = [], 0, 0, False
    paths = sorted((run / 'profile').rglob('events.jsonl'))
    with gzip.open(target, 'wt', encoding='utf-8') as dest:
        for path in paths:
            with path.open(encoding='utf-8') as source:
                for line in source:
                    if not line.strip():
                        continue
                    row = json.loads(line)
                    sessions += row['event'] == 'session_start'
                    ends += row['event'] == 'session_end'
                    capped |= row['event'] == 'capture_limit'
                    projected = project_event(row)
                    if projected:
                        dest.write(canonical(projected) + '\n')
                        events.append(projected)
    status = 'capped' if capped else 'complete' if paths and sessions == ends == len(paths) else 'missing_or_truncated'
    return events, status


def phase_problems(lines, samples, result, label, starts):
    planned = match.markers(lines, 'GENOME_CONFIG')
    applied = match.markers(lines, 'GENOME_PHASE')
    loaded = [s for s in samples if s['phase'] == 'loaded' and s['bots'] and not s['other_controllers']]
    expected = sum(s['bots'] for s in loaded)
    if len([p for p in planned if p['phase'] == 'opening']) != expected or not expected:
        return ['missing_controller_phase_config'], set()
    problems, houses = [], set()
    for p in planned:
        key = p['house'], p['controller'], p['phase']
        start = int(result.get('start', 0)) + starts[match.PHASES.index(p['phase'])] * CYCLES_PER_MINUTE
        if p['genome'] != label or int(p['start_cycle']) != start:
            problems.append('phase_schedule_mismatch')
        actual = [a for a in applied if (a['house'], a['controller'], a['phase']) == key]
        if start <= int(result.get('cycle', 0)) and (len(actual) != 1 or
                int(actual[0]['cycle']) != start or actual[0]['config_hash'] != p['config_hash']):
            problems.append('missing_phase_application')
        if actual:
            houses.add(int(p['house']))
    return sorted(set(problems)), houses


def play(args, scratch, binary, key):
    case_id, label = key
    registered, data, saved_hash, context = frozen_input(args)
    if registered != case_id:
        raise ValueError('Scheduled and frozen cases disagree')
    candidate, registry = manifest(args.manifest_b64)[label], genome_model.load_registry()
    identity = genome_model.genome_id(candidate)
    tag = case_id + '-' + label + '-' + identity[:12]
    out = Path(scratch) / tag
    out.mkdir(parents=True)
    saved = out / 'original.dls'
    saved.write_bytes(data)
    profile = out / 'fixture-profile'
    restore_fixture_revisions(profile, [saved])
    phases, run = out / 'phases', out / 'run'
    receipts = match.render_phase_inputs([(h, label) for h in range(12)], dict(context, difficulty='brutal'),
        {label: candidate}, phases, tuple(args.phase_minutes), registry, elapsed=True, difficulty_variants=True)
    env = dict(os.environ, BALANCE_GENOME_BEHAVIOR='1')
    env.pop('BALANCE_TECHNOLOGY_INSTALLMENTS', None)
    if 'technology_installments' in candidate.get('values', {}):
        env['BALANCE_TECHNOLOGY_INSTALLMENTS'] = str(candidate['values']['technology_installments'])
    command = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
        '--prebuilt-probe', str(binary), '--output-dir', str(run), '--load-save', str(saved), '--save-profile', str(profile),
        '--mod', context['mod'], '--minutes', str(definitions()[case_id]['minutes']), '--wall-timeout', str(args.wall_timeout),
        '--path-workers', '1', '--capture-mib', str(args.capture_mib), '--genome-phase-dir', str(phases)]
    started = time.monotonic()
    process = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
    lines = (run / 'run.log').read_text(errors='replace').splitlines() if (run / 'run.log').is_file() else []
    samples, setups = [], []
    for line in lines:
        for marker, target in (('GENOME_BEHAVIOR_HOUSE:', samples), ('GENOME_BEHAVIOR_EVENT:', samples), ('GENOME_BEHAVIOR_SETUP:', setups)):
            if marker in line:
                target.append(json.loads(line.split(marker, 1)[1]))
    result = next(iter(match.markers(lines, 'GENOME_BEHAVIOR_RESULT')), {})
    setup = setups[0] if len(setups) == 1 else {}
    destination = Path(args.results)
    destination.mkdir(parents=True, exist_ok=True)
    events, telemetry = capture_events(run, destination / (tag + '-events.jsonl.gz'))
    if result.get('capture_limited') == '1' or result.get('telemetry_enabled') != '1':
        telemetry = 'capped_or_disabled'
    problems, applied = phase_problems(lines, samples, result, label, args.phase_minutes)
    if process.returncode or not result:
        problems.append('rc=%d/missing_result' % process.returncode)
    if not setup or (setup.get('mode'), setup.get('format'), max(setup.get('width', 0), setup.get('height', 0))) != (context['mod'], context['format'], context['size']):
        problems.append('saved_context_mismatch')
    expected_installments = candidate.get('values', {}).get('technology_installments',
        definitions()[case_id]['game_rules']['technology_installments'])
    if setup.get('technology_installments') != expected_installments:
        problems.append('game_rule_mismatch')
    summary = json.loads((run / 'summary.json').read_text()) if (run / 'summary.json').is_file() else {}
    source = summary.get('sourceCommit', '')
    if len(source) != 40 or any(c not in '0123456789abcdef' for c in source):
        problems.append('immutable_source_provenance_missing')
    completed = not problems and result.get('completed') == '1'
    checks, feasibility = evaluate_case(definitions()[case_id], samples, events, setup, completed, applied, telemetry)
    for row in checks:
        row.update(case=case_id, genome=label, genome_digest=identity, source_commit=source)
    checks_file, evidence_file = tag + '-checks.csv', tag + '-evidence.json.gz'
    append(destination / checks_file, CHECK_FIELDS, checks)
    retained = []
    for path in (run / 'summary.json', phases / 'effective-options.ini', phases / 'map.ini'):
        if path.is_file():
            name = tag + '-' + path.name + '.gz'
            match.compressed_copy(path, destination / name)
            retained.append(dict(file=name, source_sha256=match.file_digest(path)))
    evidence = dict(schema_version=1, setup=setup, result=result, samples=samples, phase_receipts=receipts,
        phase_configs=match.markers(lines, 'GENOME_CONFIG'), phase_applications=match.markers(lines, 'GENOME_PHASE'),
        genome=candidate, context=context, phase_minutes=args.phase_minutes, files=retained,
        command=command, runner_output=process.stdout + process.stderr, problems=problems)
    with gzip.open(destination / evidence_file, 'wt', encoding='utf-8') as stream:
        stream.write(canonical(evidence) + '\n')
    row = dict(case=case_id, genome=label, genome_digest=identity, save_sha256=saved_hash,
        input_sha256=match.file_digest(args.game_options), source_commit=source, source_fingerprint=summary.get('sourceFingerprint', ''),
        probe_sha256=match.file_digest(binary), registry_sha256=match.digest(registry),
        checks_sha256=match.file_digest(Path(__file__).with_name('genome-behavior-checks.json')),
        technology_installments={0: 'off', 1: 'on'}.get(setup.get('technology_installments'), ''),
        completed=int(completed), telemetry=telemetry,
        feasibility=feasibility, wall_s=round(time.monotonic() - started, 3), error=';'.join(problems),
        checks_file=checks_file, evidence_file=evidence_file)
    append(destination / 'games.csv', FIELDS, [row])
    finish_game(scratch, out, bool(problems))
    # Cluster finish_game retains a game's directory for transport; do not leave
    # its raw events/profile there to duplicate the projected evidence archive.
    if not problems:
        shutil.rmtree(out, ignore_errors=True)
    return row


def report(args):
    rows = read(Path(args.results) / 'games.csv')
    for row in rows:
        print('%s %s: %s source=%s telemetry=%s wall=%ss error=%s' %
            (row['case'], row['genome'], row['feasibility'].upper(), row['source_commit'], row['telemetry'], row['wall_s'], row['error'] or 'none'))
        for check in read(Path(args.results) / row['checks_file']):
            print('  house=%s %s: %s %s' % (check['house'], check['check'], check['status'].upper(), check['details']))
    # Infeasible is a real measurement, not an infrastructure error to rerun.
    return int(not rows or any(row['error'] for row in rows))


def run(args):
    if not args.results:
        raise ValueError('--results is required')
    done = {tuple(r[k] for k in KEY) for r in read(Path(args.results) / 'games.csv')}
    todo = [key for key in schedule(args) if key not in done]
    if todo:
        require_free_space()
        with scratch_dir('genome-behaviors', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            for key in todo:
                play(args, scratch, binary, key)
    return report(args)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', type=Path)
parser.add_argument('--case', choices=tuple(definitions()))
parser.add_argument('--game-options', type=Path)
parser.add_argument('--manifest-b64')
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--scratch', type=Path)
parser.add_argument('--rebuild', action='store_true')
parser.add_argument('--wall-timeout', type=int, default=3600)
parser.add_argument('--capture-mib', type=int, default=1024)
parser.add_argument('--phase-minutes', type=int, nargs=3, default=[0, 10, 20])
parser.add_argument('--freeze', type=Path, metavar='PRIVATE_SAVE')
parser.add_argument('--output', type=Path)
parser.add_argument('--report', action='store_true')
args = parser.parse_args()
try:
    if args.freeze:
        if not args.case or not args.output:
            raise ValueError('--freeze requires --case and --output')
        print(canonical(freeze(args.freeze, args.case, args.output)))
    else:
        raise SystemExit(report(args) if args.report else run(args))
except ValueError as error:
    parser.error(str(error))
