#!/usr/bin/env python3
"""Fixed control maps with one inline registered genome for every QuantBot.

A promoted genome flips registry defaults for every QuantBot, so this mirrors the
controls tier's explicit --style convention: every QuantBot house (never a classic
AI Player) gets the same constant overrides through per-house phase INIs. The
scenario-matrix schedule, rows and compare-control-maps.py remain authoritative;
native omits --sweep-genome-b64 and is then exactly scenario-matrix's controls run.
"""
import ast
import base64
import json
import re
import threading
from functools import lru_cache
from pathlib import Path

import genome_model


def load_matrix():
    """Use the same top-level argument boundary as the queue's harness loader."""
    path = Path(__file__).with_name('scenario-matrix.py')
    nodes = []
    for node in ast.parse(path.read_text(encoding='utf-8'), str(path)).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        nodes.append(node)
    else:
        raise ValueError('Missing scenario-matrix argument boundary')
    namespace = {'__file__': str(path), '__name__': 'cluster_harness'}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), 'exec'), namespace)
    return namespace


MATRIX = load_matrix()
BASE_SCHEDULE = MATRIX['schedule']
BASE_COMMAND = MATRIX['command']
BASE_PLAY = MATRIX['play']
BASE_FINISH = MATRIX['finish_game']
QUANTBOTS = ('QB', 'QH', 'QM', 'QE')
RECEIPTS = 'sweep-receipts.csv'
RECEIPT_FIELDS = ('id', 'genome', 'houses', 'verified', 'reason')
parser = MATRIX['parser']
parser.add_argument('--sweep-genome-b64',
                    help='Base64 JSON registered genome for every QuantBot (controls tier only; omit for native)')
read = MATRIX['read']
pending, pending_lock = {}, threading.Lock()


@lru_cache(maxsize=32)
def sweep_genome(encoded):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate genome JSON key: ' + key)
            result[key] = value
        return result

    raw = base64.b64decode(encoded + '=' * (-len(encoded) % 4), altchars=b'-_', validate=True)
    genome = json.loads(raw.decode('utf-8'), object_pairs_hook=unique)
    registry = genome_model.load_registry()
    genome_model.validate_genome(genome, registry)
    if genome.get('expression'):
        raise ValueError('Control-map genomes are constant registry flips; no contextual expression')
    rules = {gene['id'] for gene in registry if genome_model.is_game_rule(gene)}
    if rules & set(genome.get('values', {})):
        raise ValueError('Sweep genomes cannot change whole-game rules')
    return genome, registry


def label(genome):
    return 'sweep-' + genome_model.genome_id(genome)[:16]


def configured(args):
    if args.tier != 'controls':
        raise ValueError('sweep-controls.py plays only --tier controls')
    if getattr(args, 'style', None) or getattr(args, 'genome', None) or getattr(args, 'manifest_b64', None):
        raise ValueError('Use --sweep-genome-b64 alone, without --style or qualification genomes')
    return args


def quantbot_houses(game):
    """House number -> (house name, difficulty) for every QuantBot side."""
    return {str(MATRIX['HOUSE_NUMBERS'][house]): (house, MATRIX['DIFFICULTY'][code])
            for house, _, code in game['sides'] if code in QUANTBOTS}


def phase_inputs(args, game, out):
    genome, registry = sweep_genome(args.sweep_genome_b64)
    houses = quantbot_houses(game)
    if not houses:
        raise ValueError('A control game without QuantBots: ' + game['id'])
    dimensions = re.search(r'(\d+)x(\d+)', game.get('map', ''))
    size = max(int(dimensions[1]), int(dimensions[2])) if dimensions else 128
    directory = out.with_suffix('.sweep')
    directory.mkdir(parents=True, exist_ok=True)
    for number, (house, difficulty) in houses.items():
        # No expression is accepted, so this context cannot select different values.
        context = dict(difficulty=difficulty, mod=game['mod'], house=house, format='controls',
                       size=size, phase='opening')
        payload = genome_model.render_ini(genome, context, registry)
        for phase in ('opening', 'mid', 'late'):
            (directory / f'{number}-{phase}.ini').write_text(payload, encoding='utf-8')
    name = label(genome)
    (directory / 'schedule.ini').write_text(
        '[Schedule]\nopening=0\nmid=9999\nlate=10000\n\n[Genomes]\n'
        + ''.join(f'{number}={name}\n' for number in sorted(houses, key=int)), encoding='utf-8')
    with pending_lock:
        pending[str(out)] = (game['id'], name, sorted(houses, key=int), Path(args.results))
    return directory


def verify(lines, name, houses):
    """Every QuantBot, and only they, applied the exact genome before play started."""
    parse = MATRIX['parse']
    setup = next((parse(line, 'SCENARIO_SETUP:') for line in lines if 'SCENARIO_SETUP:' in line), {})
    configs = [parse(line, 'SCENARIO_AI_CONFIG:') for line in lines if 'SCENARIO_AI_CONFIG:' in line]
    phases = [parse(line, 'GENOME_PHASE:') for line in lines if 'GENOME_PHASE:' in line]
    hashes = {row.get('house'): row.get('config_hash') for row in configs}
    if setup.get('style') != 'default':
        return False, 'style_not_default'
    if len(hashes) != len(configs) or set(hashes) != set(houses):
        return False, 'quantbot_roster_mismatch'
    if sorted(row.get('house') for row in phases) != sorted(houses):
        return False, 'phase_receipts_mismatch'
    if any(row.get('genome') != name or row.get('phase') != 'opening' or row.get('cycle') != '0'
           or not row.get('config_hash') or row.get('config_hash') != hashes[row['house']] for row in phases):
        return False, 'phase_receipt_invalid'
    return True, ''


def command(args, binary, game, out):
    cmd = BASE_COMMAND(args, binary, game, out)
    if getattr(args, 'sweep_genome_b64', None):
        cmd += ['--genome-phase-dir', str(phase_inputs(args, game, out))]
    return cmd


def finish_game(scratch, out, failed):
    with pending_lock:
        entry = pending.pop(str(out), None)
    if entry is not None:
        game_id, name, houses, results = entry
        log = Path(out) / 'run.log'
        lines = log.read_text(errors='replace').splitlines() if log.exists() else []
        verified, reason = verify(lines, name, houses) if lines else (False, 'missing_run_log')
        MATRIX['append'](results / RECEIPTS, RECEIPT_FIELDS, [dict(
            id=game_id, genome=name, houses=' '.join(houses), verified=int(verified), reason=reason)])
        if verified and not failed:
            MATRIX['remove_tree'](Path(out).with_suffix('.sweep'))
    return BASE_FINISH(scratch, out, failed)


def schedule(args):
    return BASE_SCHEDULE(configured(args))


def build_probe(args, scratch):
    return MATRIX['build_probe'](configured(args), scratch)


def play(args, scratch, binary, game):
    return BASE_PLAY(configured(args), scratch, binary, game)


def run(args):
    # Keep the queue's compressed-table reader for the final completeness check.
    MATRIX['read'] = read
    MATRIX['run'](configured(args))
    if getattr(args, 'sweep_genome_b64', None):
        receipts = {row['id']: row for row in read(Path(args.results) / RECEIPTS)}
        problems = [game['id'] for game in schedule(args) if receipts.get(game['id'], {}).get('verified') != '1']
        if problems:
            raise SystemExit('Unverified sweep genome application: ' + ', '.join(problems))


MATRIX['command'] = command
MATRIX['finish_game'] = finish_game
args = parser.parse_args()
if args.list:
    for game in schedule(args):
        print(game['id'], game['map'], ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']))
elif not args.results:
    parser.error('--results is required to play games')
else:
    run(args)
