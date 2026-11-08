#!/usr/bin/env python3
"""Candidate-only registered genomes on the ordinary audit's paired sample deck.

Only constant sparse QuantBot overlays are admitted. The audit remains the owner
of fixtures, engine invocation, native opponents, outcome scoring and receipts.
One manifest of genomes is one queue job; every genome replays the same deck.
"""
import ast
import base64
import copy
import json
import os
from functools import lru_cache
from pathlib import Path

import genome_model


def load_audit():
    """Use the same top-level argument boundary as genome-match and the queue."""
    path = Path(__file__).with_name('dunecity-audit.py')
    nodes = []
    for node in ast.parse(path.read_text(encoding='utf-8'), str(path)).body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
            break
        nodes.append(node)
    else:
        raise ValueError('Missing audit argument boundary')
    namespace = {'__file__': str(path), '__name__': 'cluster_harness'}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(path), 'exec'), namespace)
    return namespace


AUDIT = load_audit()
BASE_SCHEDULE = AUDIT['schedule']
BASE_PLAY = AUDIT['play']
BASE_INPUTS = AUDIT['seat_style_inputs']
INHERITED = ('BALANCE_SCENARIO_STYLE', 'BALANCE_GENOME_PHASE_DIR', 'DUNECITY_QUANTBOT_OVERLAY')
parser = AUDIT['parser']
parser.add_argument('--candidate-manifest-b64',
                    help='Base64 JSON {"schema_version":1,"genomes":[...]}; each genome plays the whole deck '
                         'in the candidate seat only. Omit for the native arm.')
read = AUDIT['read']


def style(genome):
    return 'sweep-' + genome_model.genome_id(genome)


@lru_cache(maxsize=16)
def candidate_manifest(encoded):
    """Registered, constant, distinct genomes in submission order."""
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate manifest JSON key: ' + key)
            result[key] = value
        return result

    raw = base64.b64decode(encoded + '=' * (-len(encoded) % 4), altchars=b'-_', validate=True)
    manifest = json.loads(raw.decode('utf-8'), object_pairs_hook=unique)
    if (not isinstance(manifest, dict) or set(manifest) != {'schema_version', 'genomes'}
            or manifest['schema_version'] != 1 or not isinstance(manifest['genomes'], list)
            or not manifest['genomes']):
        raise ValueError('Manifest must be {"schema_version":1,"genomes":[at least one genome]}')
    registry = genome_model.load_registry()
    rules = {gene['id'] for gene in registry if genome_model.is_game_rule(gene)}
    genomes, styles = {}, set()
    for genome in manifest['genomes']:
        genome_model.validate_genome(genome, registry)
        assigned = set(genome.get('values', {}))
        for rule in genome.get('expression', []):
            if rule.get('when', {}).get('phase'):
                raise ValueError('Sweep genomes must be constant within a game; no phase predicates')
            assigned.update(rule['values'])
        if assigned & rules:
            raise ValueError('Sweep genomes cannot change whole-game rules')
        if not assigned:
            raise ValueError('A candidate genome must assign at least one gene; native omits the manifest')
        if genome['id'] in genomes or style(genome) in styles:
            raise ValueError('Duplicate candidate genome: ' + genome['id'])
        genomes[genome['id']] = genome
        styles.add(style(genome))
    return genomes, registry


def configured(args):
    if args.mode != 'sample' or args.mod not in ('vanilla', 'dunecity', 'both'):
        raise ValueError('SweepLane requires --mode sample --mod vanilla|dunecity|both')
    if not 1 <= args.minutes <= 120 or args.cases < 1 or not 0 <= args.sample_seed <= 0xffffffff:
        raise ValueError('Use positive cases, 1-120 minutes and an unsigned 32-bit sample seed')
    if args.style or args.candidate_style or args.seat_styles:
        raise ValueError('SweepLane uses only --candidate-manifest-b64, not play-style overlays')
    if args.candidate_manifest_b64:
        candidate_manifest(args.candidate_manifest_b64)
    return args


def schedule(args):
    deck = BASE_SCHEDULE(configured(args))
    if not args.candidate_manifest_b64:
        return deck
    genomes, _ = candidate_manifest(args.candidate_manifest_b64)
    games = []
    for name, genome in genomes.items():
        label = style(genome)
        # Same fixture, controller seats and seeds; only the candidate seat's config moves.
        games.extend(dict(game, id=game['id'] + '/' + label[:22], candidate_style=label, sweep_genome=name)
                     for game in deck)
    return games


def genome_inputs(args, game, out):
    if not game.get('sweep_genome'):
        return BASE_INPUTS(args, game, out)
    genomes, registry = candidate_manifest(args.candidate_manifest_b64)
    genome = genomes[game['sweep_genome']]
    if game['candidate_style'] != style(genome):
        raise ValueError('Scheduled genome identity changed: ' + game['id'])
    house = next(name for name, _, _ in game['sides']
                 if AUDIT['HOUSE_IDS'][name] == game['candidate_house'])
    context = dict(difficulty='brutal', mod=game['mod'], house=house,
                   format=game['format'], size=game['size'], phase='opening')
    payload = genome_model.render_ini(genome, context, registry)
    directory = out.with_suffix('.candidate')
    directory.mkdir(parents=True, exist_ok=True)
    for phase in ('opening', 'mid', 'late'):
        (directory / f'{game["candidate_house"]}-{phase}.ini').write_text(payload, encoding='utf-8')
    (directory / 'schedule.ini').write_text(
        '[Schedule]\nopening=0\nmid=9999\nlate=10000\n\n[Genomes]\n'
        + f'{game["candidate_house"]}=candidate-{game["candidate_style"]}\n', encoding='utf-8')
    return directory


def build_probe(args, scratch):
    # Cluster preparation supplies only build-dir/results, not the eventual fixture arguments.
    return AUDIT['build_probe'](args, scratch)


def play(args, scratch, binary, game):
    # Native and candidate arms never inherit another experiment's overlay environment.
    for key in INHERITED:
        os.environ.pop(key, None)
    return BASE_PLAY(configured(args), scratch, binary, game)


def run(args):
    # Preserve the queue's compressed-table reader when reporting retained results.
    AUDIT['read'] = read
    return AUDIT['run'](configured(args))


AUDIT['schedule'] = schedule
AUDIT['play'] = play
AUDIT['seat_style_inputs'] = genome_inputs
args = parser.parse_args()
if args.report_dirs:
    AUDIT['report'](args.report_dirs)
elif args.compare:
    AUDIT['compare'](*args.compare)
elif args.list:
    for game in schedule(args):
        print(json.dumps(copy.deepcopy(game), sort_keys=True))
elif not args.results:
    parser.error('--results is required to play games')
else:
    run(args)
