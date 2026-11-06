#!/usr/bin/env python3
"""Test-only QuantBot screening, constrained league search and evidence analysis.

See docs/ai-genome.md. Python 3.9; exact rational inference and integer operators.
The shipped game does not import this module and this tool never changes defaults.
"""
import argparse
import base64
import copy
import csv
import gzip
import hashlib
import itertools
import json
import random
import re
import subprocess
import sys
import time
from collections import defaultdict
from fractions import Fraction
from pathlib import Path

import genome_model

ROOT = Path(__file__).resolve().parents[2]
CONTROL = 'default'
CONTEXT = ('mod', 'difficulty', 'research', 'format', 'size', 'phase', 'base', 'spice')
OBJECTIVES = ('win_score', 'kill_value', 'alive_followup_minute', 'army_value_time_mean',
              'special_built', 'attack_count', 'special_live_value_share', 'idle_money_time_mean',
              'economy_stall_fraction', 'base_loss_fraction', 'population', 'research_level')
QUALITY = ('adopt_light_factory', 'adopt_heavy_factory', 'adopt_ix', 'adopt_starport',
           'adopt_palace', 'special_built', 'attack_count', 'economy_stall_fraction', 'base_loss_fraction')
CITY_QUALITY = ('zone_residential', 'zone_industrial', 'zone_commercial', 'population')
LOWER_BETTER = frozenset(('economy_stall_fraction', 'base_loss_fraction', 'passive_fraction',
                          'unused_credits_fraction', 'first_attack_minute', 'idle_money_time_mean'))


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def digest(value):
    return hashlib.sha256(canonical(value).encode('utf-8')).hexdigest()


def save_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix == '.gz':
        with gzip.open(str(path), 'wt', encoding='utf-8') as stream:
            json.dump(value, stream, sort_keys=True, indent=2)
            stream.write('\n')
    else:
        path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n', encoding='utf-8')


def load_json(path):
    path = Path(path)
    if path.suffix == '.gz':
        with gzip.open(str(path), 'rt', encoding='utf-8') as stream:
            return json.load(stream)
    return json.loads(path.read_text(encoding='utf-8'))


def load_rows(path):
    opener = gzip.open if str(path).endswith('.gz') else open
    with opener(str(path), 'rt', encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream))


def write_rows(path, fields, rows):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(str(path), 'wt', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)


def mean(values):
    values = list(values)
    return sum(values, Fraction()) / len(values) if values else None


def rational(value):
    if value is None:
        return ''
    value = Fraction(value)
    return str(value.numerator) if value.denominator == 1 else '%d/%d' % (value.numerator, value.denominator)


def decimal(value, places=4):
    if value is None:
        return 'NA'
    value = Fraction(value)
    scaled = abs(value) * 10 ** places
    rounded = (scaled.numerator * 2 + scaled.denominator) // (scaled.denominator * 2)
    text = '%d.%0*d' % (rounded // 10 ** places, places, rounded % 10 ** places)
    return ('-' if value < 0 else '') + text


def selected_genes(registry, names=None, switches_only=False):
    known = {gene['id']: gene for gene in registry}
    if names:
        missing = sorted(set(names) - set(known))
        if missing:
            raise ValueError('Unknown genes: ' + ', '.join(missing))
        return [known[name] for name in names]
    return [gene for gene in registry if len(gene['levels']) > 1
            and (not switches_only or gene['kind'] == 'bool')]


def alternative(gene):
    others = [level for level in gene['levels'] if level != gene['default']]
    if not others:
        raise ValueError('No alternate level for ' + gene['id'])
    if gene['kind'] in ('int', 'decimal', 'bool'):
        base = Fraction(str(gene['default']))
        return max(others, key=lambda level: (abs(Fraction(str(level)) - base), str(level)))
    return others[0]


def genome(name, values=None, expression=None):
    return dict(id=name, values=values or {}, expression=expression or [])


def fractional_design(genes):
    """Resolution-IV fraction: no main column aliases a product of two columns.

    Highest-bit-set Walsh columns contain their own global foldover. Two-factor
    products CAN alias each other; investigate them with the separate 2x2 design.
    """
    if not genes:
        return [], {}
    half = 2
    while half < len(genes):
        half *= 2
    columns = {gene['id']: half + index for index, gene in enumerate(genes)}
    rows = [{name: 1 if bin(row & column).count('1') % 2 == 0 else -1
             for name, column in columns.items()} for row in range(half * 2)]
    return rows, columns


def screen_plan(registry, genes, parameter_sweeps=True, fraction=True):
    genomes, designs = [genome(CONTROL)], []
    for gene in genes:
        levels = [level for level in gene['levels'] if level != gene['default']]
        if not parameter_sweeps:
            levels = [alternative(gene)]
        for index, level in enumerate(levels):
            name = 'one-%s-%d' % (gene['id'], index)
            genomes.append(genome(name, {gene['id']: level}))
            designs.append(dict(genome=name, stage='one_gene', gene=gene['id'],
                                baseline=gene['default'], alternative=level, binary=gene['kind'] == 'bool'))
    columns = {}
    if fraction:
        signs_list, columns = fractional_design(genes)
        for index, signs in enumerate(signs_list):
            name = 'fraction-%04d' % index
            values = {gene['id']: alternative(gene) for gene in genes if signs[gene['id']] == 1}
            genomes.append(genome(name, values))
            designs.append(dict(genome=name, stage='fractional', signs=signs))
    for item in genomes:
        genome_model.validate_genome(item, registry)
    aliases = defaultdict(list)
    for left, right in itertools.combinations(columns, 2):
        aliases[columns[left] ^ columns[right]].append([left, right])
    return dict(schema_version=1, kind='screen', registry_sha256=digest(registry),
                search_genes=[gene['id'] for gene in genes], genomes=genomes, designs=designs,
                fractional_columns=columns, fractional_resolution=4 if columns else None,
                interaction_alias_groups=list(aliases.values()), independent_unit='common map-seed block')


def interaction_plan(registry, names):
    genes = selected_genes(registry, names)
    genomes, designs = [genome(CONTROL)], []
    for left, right in itertools.combinations(genes, 2):
        variants = {}
        for a, b in itertools.product((0, 1), repeat=2):
            name = 'pair-%s-%s-%d%d' % (left['id'], right['id'], a, b)
            values = {}
            if a:
                values[left['id']] = alternative(left)
            if b:
                values[right['id']] = alternative(right)
            genomes.append(genome(name, values))
            variants['%d%d' % (a, b)] = name
        designs.append(dict(stage='interaction', genes=[left['id'], right['id']], variants=variants))
    return dict(schema_version=1, kind='interactions', registry_sha256=digest(registry),
                genomes=genomes, designs=designs)


def add_protocol(plan, args):
    contexts = [dict(mod=mod, difficulty=difficulty, size=size, base=base, spice=spice, research=research)
                for mod, difficulty, size, base, spice, research in itertools.product(
                    args.mods, args.difficulties, args.sizes, args.bases, args.spice, args.research)]
    plan['protocol'] = dict(contexts=contexts, seeds=args.seeds, minutes=args.minutes, modes=args.modes,
                            phase_minutes=[0, args.opening_end, args.mid_end],
                            houses=getattr(args, 'houses', ['harkonnen', 'atreides']),
                            ffa_houses=getattr(args, 'ffa_houses', ['harkonnen', 'atreides', 'ordos', 'sardaukar']),
                            rotations='1v1: genotype-to-house x house-to-slot (4); 4P: 4 x 4 (16)',
                            ships_defaults=False)
    plan['plan_sha256'] = digest(plan)
    return plan


def bootstrap_interval(values, repetitions=4096, seed=20261106):
    values = list(values)
    center = mean(values)
    if len(values) < 4:
        return center, None, None
    rng = random.Random(seed)
    samples = sorted(mean(values[rng.randrange(len(values))] for _ in values) for _ in range(repetitions))
    return center, samples[repetitions // 40], samples[repetitions - repetitions // 40 - 1]


def simultaneous_intervals(effects, repetitions=4096, seed=20261106):
    """Shared-seed max-deviation bootstrap, all contrasts in a metric family.

    Incomplete seed sets or <4 independent blocks receive no interval. Houses,
    rotations and minute snapshots never masquerade as independent replications.
    """
    if not effects:
        return {}
    common = set.intersection(*(set(values) for values in effects.values()))
    if len(common) < 4 or any(set(values) != common for values in effects.values()):
        return {key: (mean(values.values()), None, None) for key, values in effects.items()}
    seeds = sorted(common)
    centers = {key: mean(values.values()) for key, values in effects.items()}
    rng, counts = random.Random(seed), defaultdict(int)
    for _ in range(repetitions):
        histogram = [0] * len(seeds)
        for _ in seeds:
            histogram[rng.randrange(len(seeds))] += 1
        counts[tuple(histogram)] += 1
    maxima = []
    for histogram, count in counts.items():
        radius = max(abs(sum((values[s] * number for s, number in zip(seeds, histogram)), Fraction())
                         / len(seeds) - centers[key]) for key, values in effects.items())
        maxima.extend([radius] * count)
    maxima.sort()
    radius = maxima[min(len(maxima) - 1, len(maxima) * 95 // 100)]
    return {key: (center, center - radius, center + radius) for key, center in centers.items()}


def context_key(row, house=False):
    values = [str(row.get(field, row.get('spice_level', '') if field == 'spice' else '')) for field in CONTEXT]
    return tuple(values) + ((str(row.get('house', '')), ) if house else ())


def arm_of(row):
    return row.get('genome_id') or row.get('arm') or row.get('label') or ''


class ObservationFiles:
    """Re-iterable streaming evidence; snapshots never become independent seeds."""

    def __init__(self, paths):
        files = set()
        for path in paths:
            path = Path(path)
            files.update(path.rglob('observations-*.csv.gz') if path.is_dir() else [path])
        self.files = tuple(sorted(files))

    def __iter__(self):
        sources = set()
        for path in self.files:
            opener = gzip.open if str(path).endswith('.gz') else open
            with opener(str(path), 'rt', encoding='utf-8', newline='') as stream:
                for row in csv.DictReader(stream):
                    if row.get('schema_version', '1') != '1':
                        raise ValueError('Unsupported observation schema in ' + str(path))
                    if row.get('status') not in ('measured', 'unavailable', 'censored', 'not_applicable'):
                        raise ValueError('Missing explicit evidence status in ' + str(path))
                    if row.get('source_commit'):
                        sources.add(row['source_commit'])
                    if len(sources) > 1:
                        raise ValueError('Do not pool native revisions: use ai-baseline-gate.py for history')
                    yield row


def observations(paths):
    return ObservationFiles(paths)


def responses(rows, house=False, require_rotations=False):
    grouped = defaultdict(lambda: defaultdict(lambda: [Fraction(), 0]))
    seen_games = defaultdict(set)
    unavailable = defaultdict(int)
    for row in rows:
        if str(row.get('minute', '-1')) not in ('', '-1') and row.get('phase', 'all') == 'all':
            continue
        arm, metric, context = arm_of(row), row['metric'], context_key(row, house)
        if row['status'] != 'measured' or row.get('value', '') == '':
            unavailable[(arm, metric, context)] += 1
            continue
        if house and not row.get('comparison_id'):
            unavailable[(arm, metric, context)] += 1
            continue
        game = row['comparison_id'] if house else (row.get('game_id') or row.get('id', ''))
        cell = grouped[(game, row.get('seed', ''), metric, context)][arm]
        cell[0] += Fraction(row['value'])
        cell[1] += 1
        if house:
            seen_games[(game, row.get('seed', ''), metric, context, arm)].add(row.get('game_id') or row.get('id', ''))
    contrasts, absolute = defaultdict(lambda: defaultdict(list)), defaultdict(lambda: defaultdict(list))
    for (game, seed, metric, context), by_arm in grouped.items():
        if house and require_rotations:
            complete = all(len(seen_games[(game, seed, metric, context, arm)]) ==
                           (12 if context[3] == 'ffa' and arm == CONTROL else 4 if context[3] == 'ffa' else 2)
                           for arm in by_arm)
            if not complete:
                continue
        control_cell = by_arm.get(CONTROL)
        control = control_cell[0] / control_cell[1] if control_cell else None
        for arm, (total, count) in by_arm.items():
            value = total / count
            absolute[(arm, metric, context)][seed].append(value)
            if arm != CONTROL and control is not None:
                contrasts[(arm, metric, context)][seed].append(value - control)
    reduce = lambda source: {key: {seed: mean(values) for seed, values in seeds.items()}
                             for key, seeds in source.items()}
    paired = reduce(contrasts)
    if require_rotations and not house:
        paired = {key: {seed: value for seed, value in seeds.items()
                        if len(contrasts[key][seed]) == (16 if key[2][3] == 'ffa' else 4)}
                  for key, seeds in paired.items()}
        paired = {key: seeds for key, seeds in paired.items() if seeds}
    return paired, reduce(absolute), unavailable


def effect_contrasts(plan, measured):
    by_arm = defaultdict(list)
    for (arm, metric, context), seeds in measured.items():
        by_arm[arm].append((metric, context, seeds))
    out = {}
    for design in plan.get('designs', []):
        if design['stage'] == 'one_gene':
            sign = -1 if design.get('binary') and design['baseline'] == 1 else 1
            name = '%s:%s->%s' % (design['gene'], design['baseline'], design['alternative'])
            for metric, context, seeds in by_arm[design['genome']]:
                out[('one_gene', name, metric, context)] = {s: v * sign for s, v in seeds.items()}
    fractions = [d for d in plan.get('designs', []) if d['stage'] == 'fractional']
    for gene in plan.get('fractional_columns', {}):
        cells = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
        for design in fractions:
            for metric, context, seeds in by_arm[design['genome']]:
                for seed, value in seeds.items():
                    cells[(metric, context)][seed][design['signs'][gene]].append(value)
        for (metric, context), seeds in cells.items():
            expected = len(fractions) // 2
            valid = {seed: mean(sides[1]) - mean(sides[-1]) for seed, sides in seeds.items()
                     if len(sides[1]) == expected and len(sides[-1]) == expected}
            if valid:
                out[('fractional', gene, metric, context)] = valid
    for design in plan.get('designs', []):
        if design['stage'] != 'interaction':
            continue
        cells = defaultdict(lambda: defaultdict(dict))
        for code, arm in design['variants'].items():
            for metric, context, seeds in by_arm[arm]:
                for seed, value in seeds.items():
                    cells[(metric, context)][seed][code] = value
        for (metric, context), seeds in cells.items():
            valid = {seed: v['11'] - v['10'] - v['01'] + v['00']
                     for seed, v in seeds.items() if len(v) == 4}
            if valid:
                out[('interaction', '*'.join(design['genes']), metric, context)] = valid
    return out


def effect_table(plan, rows, house=False):
    measured, absolute, unavailable = responses(rows, house, require_rotations=bool(plan.get('protocol')))
    effects, families = effect_contrasts(plan, measured), defaultdict(dict)
    for key, values in effects.items():
        families[key[2]][key] = values
    intervals = {}
    for metric, family in families.items():
        intervals.update(simultaneous_intervals(family, seed=int(digest(metric)[:16], 16)))
    result = []
    for (stage, name, metric, context), values in sorted(effects.items()):
        center, low, high = intervals[(stage, name, metric, context)]
        direction = -1 if metric in LOWER_BETTER else 1
        if low is None:
            verdict = 'inconclusive_incomplete_or_fewer_than_four_seeds'
        elif (high if direction > 0 else -low) < 0:
            verdict = 'harmful_in_this_context_requires_trace_diagnosis'
        elif (low if direction > 0 else -high) > 0:
            verdict = 'helpful_in_this_context_check_interactions'
        else:
            verdict = 'inconclusive'
        result.append(dict(stage=stage, gene=name, metric=metric, context='|'.join(context),
                           seeds=len(values), effect=rational(center), low=rational(low), high=rational(high),
                           interpretation='binary one-gene ON-OFF; other contrasts alternative-default', verdict=verdict))
    represented = {entry['gene'].split(':', 1)[0] for entry in result}
    for design in plan.get('designs', []):
        if design['stage'] == 'one_gene' and design['gene'] not in represented:
            result.append(dict(stage='one_gene', gene=design['gene'], metric='', context='', seeds=0,
                               effect='', low='', high='', interpretation='', verdict='unavailable_no_complete_pairs'))
            represented.add(design['gene'])
    return result, absolute, unavailable


def dominates(left, right):
    return bool(left) and set(left) == set(right) and all(left[k] >= right[k] for k in left) and any(left[k] > right[k] for k in left)


def pareto_fronts(vectors):
    remaining, fronts = set(vectors), []
    while remaining:
        front = sorted(name for name in remaining if not any(
            dominates(vectors[other], vectors[name]) for other in remaining if other != name))
        fronts.append(front)
        remaining.difference_update(front)
    return fronts


def fitness(absolute):
    by_context, contexts = defaultdict(dict), defaultdict(set)
    for (arm, metric, context), seeds in absolute.items():
        if context[5] not in ('', 'all'):
            continue
        by_context[(arm, context)][metric] = mean(seeds.values())
        contexts[arm].add(context)
    output = {}
    for arm, strata in contexts.items():
        pooled, missing, quality, detailed = defaultdict(list), [], {}, {}
        for context in sorted(strata):
            values = by_context[(arm, context)]
            required = set(QUALITY)
            if context[0] in ('dunecity', 'city'):
                required.update(CITY_QUALITY)
            if context[2] == 'on':
                required.add('research_level')
            label = '|'.join(context)
            missing.extend(label + ':' + metric for metric in sorted(required - set(values)))
            quality[label] = {metric: rational(values[metric]) for metric in required if metric in values}
            detailed[label] = {metric: rational(values[metric]) for metric in OBJECTIVES if metric in values}
            for metric in OBJECTIVES:
                if metric in values:
                    pooled[metric].append(values[metric])
        output[arm] = dict(objectives={metric: rational(mean(values)) for metric, values in pooled.items()},
                           context_objectives=detailed, missing_quality=missing,
                           feasibility='unverified' if missing else 'requires_nonregression_gate',
                           quality=quality)
    return output


def map_elites(absolute):
    grouped, cells = defaultdict(dict), defaultdict(dict)
    for (arm, metric, context), values in absolute.items():
        if context[5] in ('', 'all'):
            grouped[(arm, context)][metric] = mean(values.values())
    for (arm, context), metrics in grouped.items():
        if metrics.get('first_attack_minute', Fraction(10000)) < 10:
            style = 'rush'
        elif metrics.get('research_level', Fraction()) >= 5:
            style = 'tech'
        elif metrics.get('population', Fraction()) >= 10000:
            style = 'economy'
        else:
            style = 'balanced'
        vector = {metric: (-metrics[metric] if metric in LOWER_BETTER else metrics[metric])
                  for metric in OBJECTIVES if metric in metrics}
        if vector:
            cells['|'.join(context[:2] + (style,))][arm] = vector
    return {cell: pareto_fronts(vectors)[0] for cell, vectors in sorted(cells.items())}


def analyse(plan, rows):
    effects, absolute, unavailable = effect_table(plan, rows)
    candidates = fitness(absolute)
    vectors = {arm: {m: (-Fraction(v) if m in LOWER_BETTER else Fraction(v)) for m, v in data['objectives'].items()}
               for arm, data in candidates.items() if data['objectives']}
    house_effects, _, _ = effect_table(plan, rows, True)
    return dict(schema_version=1, plan_sha256=plan.get('plan_sha256', digest(plan)), effects=effects,
                house_effects=house_effects, genomes=candidates, pareto_fronts=pareto_fronts(vectors),
                niches=map_elites(absolute), unavailable=[dict(genome=k[0], metric=k[1], context='|'.join(k[2]), rows=v)
                                                       for k, v in sorted(unavailable.items())],
                recommendation='KEEP_SHIPPED_DEFAULT: promotion needs held-out CIs and historical quality gate', default_changed=False)


def mutate(parent, genes, rng, step=1, contexts=None):
    child = copy.deepcopy(parent)
    picked = [gene for gene in genes if rng.randrange(max(1, len(genes))) < 2]
    if not picked:
        picked = [genes[rng.randrange(len(genes))]]
    target = child.setdefault('values', {})
    if contexts and rng.randrange(4) == 0:
        when = copy.deepcopy(contexts[rng.randrange(len(contexts))])
        rule = next((r for r in child.setdefault('expression', []) if r['when'] == when), None)
        if rule is None:
            rule = dict(when=when, values={})
            child['expression'].append(rule)
        target = rule['values']
    changed = []
    for gene in picked:
        old = target.get(gene['id'], child.get('values', {}).get(gene['id'], gene['default']))
        levels = gene['levels']
        if gene['kind'] in ('int', 'decimal'):
            levels = sorted(levels, key=lambda x: Fraction(str(x)))
            index, direction = levels.index(old), (-1 if rng.randrange(2) else 1)
            new_index = max(0, min(len(levels) - 1, index + direction * step))
            if new_index == index:
                new_index = max(0, min(len(levels) - 1, index - direction * step))
            new = levels[new_index]
        else:
            choices = [level for level in levels if level != old]
            new = choices[rng.randrange(len(choices))]
        target[gene['id']] = new
        changed.append(gene['id'])
    child['id'] = 'g-' + genome_model.genome_id(child)[:16]
    return child, changed


def crossover(left, right, genes, rng):
    values = {}
    for gene in genes:
        parent = left if rng.randrange(2) == 0 else right
        value = parent.get('values', {}).get(gene['id'], gene['default'])
        if value != gene['default']:
            values[gene['id']] = value
    # Preserve ordered context-rule blocks; reordering overlaps changes semantics.
    expression = copy.deepcopy((left if rng.randrange(2) == 0 else right).get('expression', []))
    child = genome('offspring', values, expression)
    child['id'] = 'g-' + genome_model.genome_id(child)[:16]
    return child


def pfsp_weights(payoffs, opponents):
    output = {}
    for opponent in opponents:
        record = payoffs.get(opponent, {})
        losses, games = int(record.get('losses', 0)), int(record.get('games', 0))
        if losses < 0 or games < losses:
            raise ValueError('Invalid payoff counts for ' + opponent)
        output[opponent] = max(1, 1000000 * (losses + 1) ** 2 // (games + 2) ** 2)
    return output


def weighted_choice(weights, rng):
    draw = rng.randrange(sum(weights.values()))
    for name in sorted(weights):
        draw -= weights[name]
        if draw < 0:
            return name
    raise AssertionError('Unreachable draw')


def evolve(plan, report, registry, generation, population=24, offspring=24, seed=20261106,
           payoffs=None, successes=0, attempts=0, step=1):
    if population < 4 or offspring < 1:
        raise ValueError('Population >=4 and offspring >0 required')
    genes = selected_genes(registry, plan.get('search_genes'))
    if not genes:
        raise ValueError('No mutable genes')
    rng = random.Random(seed + generation)
    genomes = {g['id']: g for g in plan['genomes']}
    ranks = {name: rank for rank, front in enumerate(report.get('pareto_fronts', [])) for name in front}
    quality = report.get('genomes', {})
    ordering = lambda name: (len(quality.get(name, {}).get('missing_quality', QUALITY)), ranks[name], name)
    ordered = sorted((name for name in ranks if name in genomes), key=ordering)
    if not ordered:
        raise ValueError('No measured genomes: synthetic or surrogate fitness is not accepted')
    selected = ordered[:population]
    for elites in report.get('niches', {}).values():
        for name in elites:
            if name in genomes and name not in selected:
                selected.append(name)
    selected = selected[:max(population, len(report.get('niches', {})))]
    parents = [genomes[name] for name in selected]
    if CONTROL in genomes and CONTROL not in selected:
        parents.insert(0, genomes[CONTROL])
    if attempts:
        step += 1 if successes * 5 > attempts else (-1 if successes * 5 < attempts else 0)
    step = max(1, min(step, max(len(g['levels']) - 1 for g in genes)))
    contexts = []
    for context in plan.get('protocol', {}).get('contexts', []):
        when = {key: [value] for key, value in context.items() if key in ('mod', 'difficulty', 'format')}
        if 'size' in context:
            when.update(size_min=context['size'], size_max=context['size'])
        contexts.append(when)
        contexts.extend(dict(phase=[phase], mod=[context['mod']], difficulty=[context['difficulty']])
                        for phase in ('opening', 'mid', 'late'))
    def tournament():
        candidates = [selected[rng.randrange(len(selected))] for _ in range(3)]
        return genomes[min(candidates, key=ordering)]
    children, lineage = [], []
    seen = {genome_model.genome_id(g) for g in parents}
    for _ in range(offspring * 100):
        if len(children) == offspring:
            break
        left, right = tournament(), tournament()
        child, changed = mutate(crossover(left, right, genes, rng), genes, rng, step, contexts)
        identity = genome_model.genome_id(child)
        if identity in seen:
            continue
        genome_model.validate_genome(child, registry)
        seen.add(identity)
        children.append(child)
        lineage.append(dict(genome=child['id'], parents=[left['id'], right['id']], mutations=changed,
                            role='main_exploiter' if len(children) <= 4 else 'main'))
    if len(children) != offspring:
        raise ValueError('Not enough unique offspring in registered search space')
    frozen = copy.deepcopy(plan.get('hall_of_fame', []))
    champion = genomes[ordered[0]]
    if genome_model.genome_id(champion) not in {genome_model.genome_id(g) for g in frozen}:
        frozen.append(copy.deepcopy(champion))
    opponents = sorted({CONTROL, champion['id']} | {g['id'] for g in frozen})
    weights, matchups = pfsp_weights(payoffs or {}, opponents), []
    for child, record in zip(children, lineage):
        targets = [champion['id']] if record['role'] == 'main_exploiter' else list(dict.fromkeys(
            (CONTROL, champion['id'], weighted_choice(weights, rng))))
        matchups.extend([child['id'], target] for target in targets if child['id'] != target)
    output = dict(schema_version=1, kind='evolution', generation=generation, registry_sha256=digest(registry),
                  genomes=list({g['id']: g for g in parents + children + frozen}.values()), hall_of_fame=frozen,
                  lineage=lineage, matchups=matchups, mutation_step=step,
                  adaptive_rule='integer 1/5 success rule on registered-level step',
                  protocol=copy.deepcopy(plan.get('protocol', {})), designs=[], search_genes=[g['id'] for g in genes],
                  pfsp_weights=weights, parents=selected)
    output['plan_sha256'] = digest(output)
    return output


def report_text(report):
    lines = ['# QuantBot genome evidence', '', report['recommendation'], '',
             'Independent seed-block contrasts; simultaneous 95% max-deviation bootstrap intervals.',
             'No interval with fewer than four complete independent seeds. Context harm is not a global disable recommendation.', '',
             '| Stage | Gene | Context | Metric | Seeds | Effect | 95% CI | Verdict |',
             '|---|---|---|---|---:|---:|---|---|']
    for row in report['effects']:
        ci = '%s .. %s' % (decimal(Fraction(row['low'])), decimal(Fraction(row['high']))) if row['low'] else 'inconclusive'
        lines.append('| %s | %s | %s | %s | %s | %s | %s | %s |' % (
            row['stage'], row['gene'], row['context'], row['metric'], row['seeds'],
            decimal(Fraction(row['effect'])) if row['effect'] else 'NA', ci, row['verdict']))
    lines += ['', '## Exploratory Pareto fronts', '']
    lines.extend('%d. %s' % (index, ', '.join(front)) for index, front in enumerate(report['pareto_fronts'], 1))
    lines += ['', '## Observed behaviour niches (Pareto sets)', '']
    lines.extend('- %s: %s' % (cell, ', '.join(elites)) for cell, elites in report['niches'].items())
    lines += ['', 'Promotion requires held-out seeds, complete behaviour telemetry and the separate historical scorecard gate.',
              'Missing/censored evidence is not zero. No shipped config is written by this tool.', '']
    return '\n'.join(lines)


def execution_batches(plan, max_manifest_bytes=24000):
    """Self-contained inline manifests; no mutable external input on cluster hosts."""
    genomes = {g['id']: g for g in plan['genomes']}
    matchups = plan.get('matchups') or [[g['id'], CONTROL] for g in plan['genomes'] if g['id'] != CONTROL]
    current, names = [], set()
    for pair in matchups:
        missing = set(pair) - set(genomes)
        if missing:
            raise ValueError('Unknown opponents: ' + ', '.join(sorted(missing)))
        wanted = names | set(pair)
        manifest = dict(schema_version=1, genomes=[genomes[name] for name in sorted(wanted)])
        if current and len(canonical(manifest).encode('utf-8')) > max_manifest_bytes:
            yield current, dict(schema_version=1, genomes=[genomes[name] for name in sorted(names)])
            current, names = [], set()
        current.append(pair)
        names.update(pair)
    if current:
        yield current, dict(schema_version=1, genomes=[genomes[name] for name in sorted(names)])


def emit_backlog(plan, args, registry=None):
    """Export once-only four-game cohorts; admission remains the runner's job."""
    if not re.fullmatch(r'[0-9a-f]{40}', args.rev):
        raise ValueError('Backlog publication requires a full immutable revision')
    payload = {key: value for key, value in plan.items() if key != 'plan_sha256'}
    plan_hash = digest(payload)
    if plan.get('plan_sha256') != plan_hash:
        raise ValueError('Plan digest does not match its immutable contents')
    if registry is None:
        source = subprocess.check_output(
            ['git', 'show', args.rev + ':tests/ai/genome-registry.json'], cwd=str(ROOT), text=True)
        registry = json.loads(source)
    registry_hash = digest(registry)
    if plan.get('registry_sha256') != registry_hash:
        raise ValueError('Plan registry differs from the pinned registry')
    protocol, entries = plan['protocol'], []
    if '1v1' not in protocol['modes']:
        raise ValueError('Idle filler supports complete 1v1 rotations only; submit FFA normally')
    houses = protocol.get('houses', ['harkonnen', 'atreides'])
    house_pairs = list(itertools.combinations(houses, 2))
    if len(set(houses)) != len(houses) or not house_pairs:
        raise ValueError('At least two distinct allowed houses are required')
    for pairs, manifest in execution_batches(plan):
        available = {item['id']: item for item in manifest['genomes']}
        for pair in pairs:
            inline = dict(schema_version=1, genomes=[available[name] for name in sorted(set(pair))])
            encoded = base64.urlsafe_b64encode(canonical(inline).encode('utf-8')).decode('ascii')
            for context in protocol['contexts']:
                for index, seed in enumerate(protocol['seeds']):
                    chosen_houses = house_pairs[index % len(house_pairs)]
                    label = args.tag + '-' + digest(dict(pair=pair, context=context, seed=seed,
                                                         houses=chosen_houses, revision=args.rev))[:20]
                    results = str((args.output / label).resolve())
                    command = ['python3', 'tests/ai/genome-match.py', '--manifest-b64', encoded,
                               '--results', results, '--tag', label, '--pairs', ':'.join(pair),
                               '--formats', '1v1', '--mods', context['mod'],
                               '--difficulties', context['difficulty'], '--sizes', str(context['size']),
                               '--bases', context['base'], '--spice', context['spice'],
                               '--research', context['research'], '--seeds', str(seed),
                               '--houses'] + list(chosen_houses)
                    command += ['--minutes', str(protocol['minutes']), '--phase-minutes']
                    command += [str(minute) for minute in protocol['phase_minutes']] + ['--evidence', 'lean']
                    entries.append(dict(id=digest(dict(revision=args.rev, command=command)),
                                        command=command, results=results, games=4,
                                        plan_sha256=plan_hash, manifest_sha256=digest(inline)))
    backlog = dict(schema_version=1, revision=args.rev, cwd=str(ROOT),
                   plan=str(args.plan.resolve()), plan_sha256=plan_hash,
                   registry_sha256=registry_hash, entries=entries)
    save_json(args.emit_backlog, backlog)
    return backlog


def run_plan(plan, args):
    if not re.fullmatch(r'[0-9a-fA-F]{7,40}', args.rev):
        raise ValueError('--rev must be an immutable SHA, not HEAD/a branch')
    if getattr(args, 'emit_backlog', None):
        return emit_backlog(plan, args)
    args.output.mkdir(parents=True, exist_ok=True)
    receipts = []
    protocol = plan['protocol']
    for index, (pairs, manifest) in enumerate(execution_batches(plan)):
        encoded = base64.urlsafe_b64encode(canonical(manifest).encode('utf-8')).decode('ascii')
        for context_index, context in enumerate(protocol['contexts']):
            for mode in protocol['modes']:
                # FFA cohorts use one candidate and three fixed opponents, rotated fairly.
                cohorts = [pairs] if mode == '1v1' else [[pair] for pair in pairs]
                for cohort_index, cohort in enumerate(cohorts):
                    label = '%s-b%03d-c%02d-%s-%02d' % (args.tag, index, context_index, mode, cohort_index)
                    destination = args.output / label
                    command = [sys.executable, 'tests/ai/genome-match.py', '--manifest-b64', encoded,
                               '--results', str(destination), '--tag', label,
                               '--difficulties', context['difficulty'], '--mods', context['mod'],
                               '--sizes', str(context['size']), '--formats', mode,
                               '--bases', context['base'], '--spice', context['spice'],
                               '--research', context['research'],
                               '--seeds'] + [str(seed) for seed in protocol['seeds']]
                    command += ['--minutes', str(protocol['minutes']), '--phase-minutes'] + [str(m) for m in protocol['phase_minutes']]
                    command += ['--evidence', getattr(args, 'evidence', 'lean')]
                    if mode == '1v1':
                        command += ['--pairs', ','.join(':'.join(pair) for pair in cohort),
                                    '--houses'] + protocol.get('houses', ['harkonnen', 'atreides'])
                    else:
                        candidate, opponent = cohort[0]
                        command += ['--ffa', ','.join((candidate, opponent, opponent, opponent)),
                                    '--houses'] + protocol.get('ffa_houses', ['harkonnen', 'atreides', 'ordos', 'sardaukar'])
                    queue = ['agent-workspace', 'run', '--project', 'dunecity', '--owner', args.owner,
                             '--tests', '--wait', '--', 'dunecity-queue', 'submit', '--cwd', str(ROOT),
                             '--rev', args.rev, '--tag', label, '--owner', args.owner, '--priority', str(args.priority), '--'] + command
                    started = time.time()
                    outcome = subprocess.run(queue, cwd=str(ROOT), check=False, text=True, capture_output=True)
                    receipt = dict(label=label, command=queue, submitted_epoch=int(started),
                                   wall_ms=int((time.time() - started) * 1000), returncode=outcome.returncode,
                                   stdout=outcome.stdout, stderr=outcome.stderr, revision=args.rev,
                                   manifest_sha256=digest(manifest), results=str(destination))
                    receipts.append(receipt)
                    save_json(args.output / 'queue-receipts.json.gz', receipts)
                    print('%s rc=%d %s' % (label, outcome.returncode, outcome.stdout.strip()), flush=True)
                    if outcome.returncode:
                        raise RuntimeError('Pinned queue submission failed; receipt retained for ' + label)
    return receipts


def parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    screen = commands.add_parser('screen')
    screen.add_argument('--genes', nargs='+')
    screen.add_argument('--switches-only', action='store_true')
    screen.add_argument('--no-fraction', action='store_true')
    screen.add_argument('--one-alternate', action='store_true')
    interactions = commands.add_parser('interactions')
    interactions.add_argument('--genes', nargs='+', required=True)
    for design in (screen, interactions):
        design.add_argument('--output', type=Path, required=True)
        design.add_argument('--mods', nargs='+', default=['vanilla', 'dune2r', 'dunecity'])
        design.add_argument('--difficulties', nargs='+', default=['medium', 'hard', 'brutal'])
        design.add_argument('--sizes', type=int, nargs='+', default=[96, 160])
        design.add_argument('--bases', nargs='+', choices=['cy', 'fortress'], default=['cy'])
        design.add_argument('--spice', nargs='+', choices=['none', 'medium'], default=['medium'])
        design.add_argument('--research', nargs='+', choices=['off', 'on'], default=['off'])
        design.add_argument('--seeds', type=int, nargs='+', default=[110601, 110602, 110603, 110604])
        design.add_argument('--minutes', type=int, default=30)
        design.add_argument('--modes', nargs='+', choices=['1v1', 'ffa'], default=['1v1', 'ffa'])
        design.add_argument('--houses', nargs='+', default=['harkonnen', 'atreides'])
        design.add_argument('--ffa-houses', nargs=4, default=['harkonnen', 'atreides', 'ordos', 'sardaukar'])
        design.add_argument('--opening-end', type=int, default=10)
        design.add_argument('--mid-end', type=int, default=20)
    analysis = commands.add_parser('analyse')
    analysis.add_argument('--plan', type=Path, required=True)
    analysis.add_argument('--observations', type=Path, nargs='+', required=True)
    analysis.add_argument('--output', type=Path, required=True)
    evolution = commands.add_parser('evolve')
    evolution.add_argument('--plan', type=Path, required=True)
    evolution.add_argument('--report', type=Path, required=True)
    evolution.add_argument('--output', type=Path, required=True)
    evolution.add_argument('--generation', type=int, required=True)
    evolution.add_argument('--population', type=int, default=24)
    evolution.add_argument('--offspring', type=int, default=24)
    evolution.add_argument('--seed', type=int, default=20261106)
    evolution.add_argument('--payoffs', type=Path)
    evolution.add_argument('--successes', type=int, default=0)
    evolution.add_argument('--attempts', type=int, default=0)
    evolution.add_argument('--mutation-step', type=int, default=1)
    run = commands.add_parser('run')
    run.add_argument('--plan', type=Path, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--rev', required=True)
    run.add_argument('--tag', required=True)
    run.add_argument('--owner', default='omp:01a19231-0a3a-776c-b1f9-f6404e42fa1c:Genome')
    run.add_argument('--priority', type=int, default=0)
    run.add_argument('--evidence', choices=('lean', 'full'), default='lean',
                     help='full only for audit samples and elites being raced')
    run.add_argument('--emit-backlog', type=Path,
                     help='Publish immutable <=4-game filler entries instead of submitting')
    return parser


def main():
    args = parser().parse_args()
    registry = genome_model.load_registry() if args.command in ('screen', 'interactions', 'evolve') else None
    if args.command == 'screen':
        genes = selected_genes(registry, args.genes, args.switches_only)
        plan = add_protocol(screen_plan(registry, genes, not args.one_alternate, not args.no_fraction), args)
        save_json(args.output, plan)
        print(canonical(dict(genomes=len(plan['genomes']), genes=len(genes), fractional_rows=sum(
            d['stage'] == 'fractional' for d in plan['designs']), contexts=len(plan['protocol']['contexts']),
            seeds=len(args.seeds), plan_sha256=plan['plan_sha256'])))
    elif args.command == 'interactions':
        save_json(args.output, add_protocol(interaction_plan(registry, args.genes), args))
    elif args.command == 'analyse':
        report = analyse(load_json(args.plan), observations(args.observations))
        args.output.mkdir(parents=True, exist_ok=True)
        save_json(args.output / 'report.json.gz', report)
        fields = ('stage', 'gene', 'metric', 'context', 'seeds', 'effect', 'low', 'high', 'interpretation', 'verdict')
        write_rows(args.output / 'gene-effects.csv.gz', fields, report['effects'])
        write_rows(args.output / 'house-effects.csv.gz', fields, report['house_effects'])
        (args.output / 'report.md').write_text(report_text(report), encoding='utf-8')
        print(report['recommendation'])
    elif args.command == 'evolve':
        result = evolve(load_json(args.plan), load_json(args.report), registry, args.generation,
                        args.population, args.offspring, args.seed, load_json(args.payoffs) if args.payoffs else None,
                        args.successes, args.attempts, args.mutation_step)
        save_json(args.output, result)
        print(canonical(dict(generation=result['generation'], offspring=len(result['lineage']),
                             frozen_opponents=len(result['hall_of_fame']), matchups=len(result['matchups']))))
    elif args.command == 'run':
        run_plan(load_json(args.plan), args)


if __name__ == '__main__':
    main()
