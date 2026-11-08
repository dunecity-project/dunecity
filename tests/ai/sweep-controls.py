#!/usr/bin/env python3
"""Fixed control maps with one inline registered genome on the candidate seat only.

Adapter over `scenario-matrix.py --tier controls`, whose `--style` already overlays just the
Brutal candidate QuantBot (slot 0) through per-house genome-phase INIs and records and
checks `style_seats` receipts. Instead of a play-style file this supplies a
registry-validated, expression-free genome rendered by genome_model and labelled
`sweep-<genotype prefix>`; every other seat stays native. Without --sweep-genome-b64 it is
exactly the native controls run. Compare either with compare-control-maps.py.
"""
import ast
import base64
import json
import re
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
BASE_STYLE_INPUTS = MATRIX['control_style_inputs']
parser = MATRIX['parser']
parser.add_argument('--sweep-genome-b64',
                    help='Base64 JSON registered genome for the candidate Brutal QuantBot seat only '
                         '(controls tier; omit for native)')
read = MATRIX['read']
finish_game = MATRIX['finish_game']
scratch_dir = MATRIX['scratch_dir']


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
    """The tier's candidate-only --style carries the genome label; a play-style file is refused."""
    if args.tier != 'controls':
        raise ValueError('sweep-controls.py plays only --tier controls')
    name = label(sweep_genome(args.sweep_genome_b64)[0]) if args.sweep_genome_b64 else None
    if getattr(args, 'style', None) not in (None, name):
        raise ValueError('Use --sweep-genome-b64 alone, without --style')
    args.style = name
    return args


def genome_style_inputs(args, game, out):
    """Candidate seat (slot 0, the Brutal QuantBot) only, exactly like the tier's style transport."""
    if not getattr(args, 'sweep_genome_b64', None):
        return BASE_STYLE_INPUTS(args, game, out)
    genome, registry = sweep_genome(args.sweep_genome_b64)
    house, _, code = game['sides'][0]
    if code != 'QB':
        raise ValueError(f'Control candidate seat is {code}, not QB: {game["id"]}')
    dimensions = re.search(r'(\d+)x(\d+)', game.get('map', ''))
    size = max(int(dimensions[1]), int(dimensions[2])) if dimensions else 128
    # No expression is accepted, so this context cannot select different values.
    context = dict(difficulty='brutal', mod=game['mod'], house=house, format='controls', size=size, phase='opening')
    payload = genome_model.render_ini(genome, context, registry)
    directory = out.with_suffix('.candidate')
    MATRIX['remove_tree'](directory)
    directory.mkdir(parents=True)
    number = MATRIX['HOUSE_NUMBERS'][house]
    for phase in ('opening', 'mid', 'late'):
        (directory / f'{number}-{phase}.ini').write_text(payload, encoding='utf-8')
    (directory / 'schedule.ini').write_text('[Schedule]\nopening=0\nmid=9999\nlate=10000\n\n[Genomes]\n'
                                            f'{number}=candidate-{args.style}\n', encoding='utf-8')
    return directory


MATRIX['control_style_inputs'] = genome_style_inputs


def schedule(args):
    return BASE_SCHEDULE(configured(args))


def build_probe(args, scratch):
    # Cluster preparation supplies only build-dir/results, not the eventual fixture arguments.
    return MATRIX['build_probe'](args, scratch)


def play(args, scratch, binary, game):
    MATRIX['finish_game'] = finish_game  # Preserve the queue's retained-log/cleanup hook.
    return MATRIX['play'](configured(args), scratch, binary, game)


def report(args):
    MATRIX['read'] = read
    return MATRIX['report'](args)


args = parser.parse_args()
if args.list:
    for game in schedule(args):
        print(game['id'], game['map'], ' '.join(f'{h}:{t}:{c}' for h, t, c in game['sides']))
elif not args.results:
    parser.error('--results is required to play games')
else:
    MATRIX['run'](configured(args))
