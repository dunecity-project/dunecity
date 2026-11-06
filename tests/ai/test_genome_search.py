"""Synthetic search/statistics contracts; never evidence about AI game strength."""
import argparse
import copy
import itertools
from fractions import Fraction
import random
import unittest
from unittest.mock import patch
import tempfile
from pathlib import Path

import genome_model
import genome_search as search


class GenomeSearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        registry = genome_model.load_registry()
        cls.registry = registry
        cls.genes = [gene for gene in registry if gene['id'] in
                     ('focus_fire', 'converge', 'wave_readiness_percent', 'attack_timer_ms')]

    @staticmethod
    def rotation_rows(houses=('harkonnen', 'atreides'), fmt='1v1', **metadata):
        rows = []
        protocol = {'houses': list(houses), 'ffa_houses': list(houses)}
        numeric = [str(search.HOUSE_NAMES.index(name)) for name in houses]
        pairs = itertools.combinations(numeric, 2) if fmt == '1v1' else [numeric]
        genomes = {'candidate': search.genome('candidate', {'focus_fire': 0}), 'default': search.genome('default')}
        for pair in pairs:
            comparison = 'pair-' + '-'.join(pair)
            signatures = search.rotation_rosters(protocol, fmt, pair, 'candidate')
            for index, signature in enumerate(sorted(signatures)):
                game = comparison + '-g' + str(index)
                for slot, house, arm in signature:
                    rows.append(dict(id='context', game_id=game, comparison_id=comparison, slot=slot, house=house,
                                     genome_digest=genome_model.genome_id(genomes[arm]), seed='1',
                                     mod='dunecity', difficulty='hard', research='off', format=fmt, size='96',
                                     phase='all', base='cy', spice_level='medium', minute='-1', metric='win_score',
                                     arm=arm, value='4' if arm == 'candidate' else '0', status='measured',
                                     **metadata))
        return rows

    def test_resolution_four_balanced_orthogonal_and_no_main_pair_alias(self):
        genes = [{'id': 'gene_%d' % index} for index in range(61)]
        rows, columns = search.fractional_design(genes)
        self.assertEqual(len(rows), 128)
        for name in columns:
            self.assertEqual(sum(row[name] for row in rows), 0)
        for left in columns:
            for right in columns:
                if left != right:
                    self.assertEqual(sum(row[left] * row[right] for row in rows), 0)
                    self.assertNotIn(columns[left] ^ columns[right], columns.values())

    def test_screen_contains_real_binary_knockouts_and_parameter_levels(self):
        plan = search.screen_plan(self.registry, self.genes)
        self.assertEqual(plan['genomes'][0]['values'], {})
        designs = [d for d in plan['designs'] if d['stage'] == 'one_gene']
        expected = sum(len(g['levels']) - 1 for g in self.genes)
        self.assertEqual(len(designs), expected)
        for item in plan['genomes']:
            genome_model.validate_genome(item, self.registry)

    def test_fraction_aliases_are_disclosed_not_interaction_claims(self):
        plan = search.screen_plan(self.registry, self.genes)
        pairs = sum(len(group) for group in plan['interaction_alias_groups'])
        self.assertEqual(pairs, len(self.genes) * (len(self.genes) - 1) // 2)
        self.assertEqual(plan['fractional_resolution'], 4)

    def test_seed_bootstrap_never_counts_rotations_as_independent_seeds(self):
        center, low, high = search.bootstrap_interval([Fraction(1), Fraction(1), Fraction(1)])
        self.assertEqual(center, 1)
        self.assertIsNone(low)
        self.assertIsNone(high)
        intervals = search.simultaneous_intervals({'a': {'1': Fraction(1), '2': Fraction(1),
                                                       '3': Fraction(1), '4': Fraction(1)}})
        self.assertEqual(intervals['a'], (1, 1, 1))

    def test_incomplete_shared_seed_design_is_inconclusive(self):
        family = {'a': {str(i): Fraction(i) for i in range(4)},
                  'b': {str(i): Fraction(i) for i in range(5)}}
        result = search.simultaneous_intervals(family)
        self.assertTrue(all(value[1:] == (None, None) for value in result.values()))

    def test_response_pairs_default_with_candidate_in_same_game(self):
        rows = []
        for orientation in range(2):
            for arm, value in (('candidate', '7'), ('default', '3')):
                rows.append(dict(id='g%d' % orientation, comparison_id='pair-a', seed='4', mod='vanilla',
                                 difficulty='hard', research='off', house=str(orientation), metric='kill_value',
                                 minute='-1', value=value, status='measured', arm=arm))
        contrasts, absolute, unavailable = search.responses(rows)
        key = next(key for key in contrasts if key[0] == 'candidate')
        self.assertEqual(contrasts[key], {'4': 4})
        self.assertFalse(unavailable)
        self.assertEqual(len(contrasts[key]), 1)

    def test_house_specific_pairs_never_mix_candidate_opponents(self):
        rows = []
        for pair, candidate, value in (('pair-a', 'a', '10'), ('pair-b', 'b', '100')):
            for arm, amount in ((candidate, value), ('default', '4')):
                rows.append(dict(id=pair + arm, comparison_id=pair, map_sha256='map', seed='1',
                                 house='0', metric='kill_value', minute='-1', value=amount,
                                 status='measured', arm=arm))
        values, _, _ = search.responses(rows, house=True)
        self.assertEqual(next(v['1'] for k, v in values.items() if k[0] == 'a'), 6)
        self.assertEqual(next(v['1'] for k, v in values.items() if k[0] == 'b'), 96)

    def test_missing_comparison_id_cannot_invent_house_effect(self):
        rows = [dict(id='x', seed='1', house='0', metric='kill_value', minute='-1',
                     value='3', status='measured', arm='a')]
        values, _, unavailable = search.responses(rows, house=True)
        self.assertFalse(values)
        self.assertEqual(sum(unavailable.values()), 1)

    def test_pareto_preserves_strength_quality_tradeoff(self):
        vectors = {'rush': {'wins': Fraction(3, 4), 'research': Fraction(1)},
                   'tech': {'wins': Fraction(1, 2), 'research': Fraction(6)},
                   'weak': {'wins': Fraction(1, 4), 'research': Fraction(0)}}
        self.assertEqual(search.pareto_fronts(vectors), [['rush', 'tech'], ['weak']])

    def test_uniform_crossover_and_discrete_mutation_are_reproducible(self):
        left = search.genome('left', {'focus_fire': 0})
        right = search.genome('right', {'converge': 0, 'attack_timer_ms': 30000})
        first = search.crossover(left, right, self.genes, random.Random(42))
        second = search.crossover(left, right, self.genes, random.Random(42))
        self.assertEqual(first, second)
        mutated, changed = search.mutate(first, self.genes, random.Random(9), contexts=[{'phase': ['late']}])
        self.assertTrue(changed)
        genome_model.validate_genome(mutated, self.registry)
        self.assertEqual(left['values'], {'focus_fire': 0})

    def test_pfsp_prioritizes_losses_and_retains_exploration(self):
        weights = search.pfsp_weights({'easy': {'losses': 0, 'games': 100},
                                       'hard': {'losses': 90, 'games': 100}}, ['easy', 'hard', 'new'])
        self.assertGreater(weights['hard'], weights['easy'])
        self.assertGreater(weights['easy'], 0)
        self.assertGreater(weights['new'], 0)
        with self.assertRaises(ValueError):
            search.pfsp_weights({'bad': {'losses': 3, 'games': 2}}, ['bad'])

    def test_evolution_rejects_unmeasured_fake_fitness(self):
        plan = dict(genomes=[search.genome('default')], search_genes=['focus_fire'])
        with self.assertRaisesRegex(ValueError, 'No measured'):
            search.evolve(plan, {'pareto_fronts': []}, self.registry, 1)

    def test_two_by_two_epistasis_is_difference_of_differences(self):
        design = dict(stage='interaction', genes=['focus_fire', 'converge'],
                      variants={'00': 'base', '10': 'a', '01': 'b', '11': 'ab'})
        context = ('vanilla', 'hard', 'off', '1v1', '96', 'all', 'cy', 'medium')
        data = {(name, 'win_score', context): {'1': Fraction(value)}
                for name, value in (('base', 0), ('a', 1), ('b', 2), ('ab', 7))}
        result = search.effect_contrasts({'designs': [design]}, data)
        self.assertEqual(next(iter(result.values()))['1'], 4)

    def test_manifest_batches_are_self_contained(self):
        plan = {'genomes': [search.genome('default'), search.genome('a', {'focus_fire': 0}),
                            search.genome('b', {'converge': 0})]}
        for pairs, manifest in search.execution_batches(plan, max_manifest_bytes=120):
            names = {g['id'] for g in manifest['genomes']}
            self.assertTrue(all(set(pair) <= names for pair in pairs))

    def test_mismatched_objective_coverage_is_not_dominance(self):
        self.assertFalse(search.dominates({'wins': 1}, {'wins': 0, 'research': 1}))

    def test_incomplete_rotations_cannot_supply_a_seed_block(self):
        rows = self.rotation_rows()
        incomplete, absolute, _ = search.responses(rows[:-2], require_rotations=True)
        self.assertFalse(incomplete)
        self.assertFalse(absolute)
        self.assertFalse(search.fitness(absolute))
        complete, absolute, _ = search.responses(rows, require_rotations=True)
        self.assertEqual(next(iter(complete.values())), {'1': 4})
        self.assertEqual({key[0] for key in absolute}, {'candidate', 'default'})
        self.assertEqual(dict(zip(search.CONTEXT, next(iter(complete))[2]))['spice'], 'medium')

    def test_rotation_fragments_cannot_pool_across_receipts_or_comparisons(self):
        for field in ('options_sha256', 'content_hash', 'object_data_hash', 'map_sha256', 'comparison_id'):
            rows = self.rotation_rows()
            for index, row in enumerate(rows):
                row[field] = 'first' if index < 4 else 'second'
            paired, absolute, _ = search.responses(rows, require_rotations=True)
            self.assertFalse(paired, field)
            self.assertFalse(absolute, field)
        rows = self.rotation_rows()
        for row in rows[-2:]:
            row.update({key: rows[0 if row['arm'] == rows[0]['arm'] else 1][key]
                        for key in ('slot', 'house')})
        self.assertFalse(search.responses(rows, require_rotations=True)[0])

    def test_three_house_and_ffa_cohorts_use_real_unique_assignments(self):
        houses = ['harkonnen', 'atreides', 'ordos']
        rows = self.rotation_rows(houses)
        paired, _, _ = search.responses(rows, require_rotations=True, protocol={'houses': houses})
        self.assertEqual(next(iter(paired.values())), {'1': 4})
        self.assertFalse(search.responses(rows[:-8], require_rotations=True, protocol={'houses': houses})[0])
        rows = self.rotation_rows(['harkonnen', 'atreides', 'ordos', 'sardaukar'], 'ffa')
        paired, _, _ = search.responses(rows, require_rotations=True)
        self.assertEqual(next(iter(paired.values())), {'1': 4})

    def test_ffa_control_needs_every_expected_measured_house(self):
        rows = self.rotation_rows(['harkonnen', 'atreides', 'ordos', 'sardaukar'], 'ffa')
        missing = next(row for row in rows if row['arm'] == 'default')
        missing.update(status='unavailable', value='')
        paired, absolute, _ = search.responses(rows, require_rotations=True)
        self.assertFalse(paired)
        self.assertFalse(absolute)

    def test_observation_digest_must_match_the_plan_genotype(self):
        rows = self.rotation_rows()
        rows[0]['genome_digest'] = 'different'
        genomes = {'candidate': search.genome('candidate', {'focus_fire': 0}), 'default': search.genome('default')}
        with self.assertRaisesRegex(ValueError, 'genotype differs'):
            search.responses(rows, require_rotations=True, genomes=genomes)

    def test_rule_and_option_strata_never_pool_for_selection_or_niches(self):
        rows = self.rotation_rows(technology_installments='off', options_sha256='off-options')
        on = self.rotation_rows(technology_installments='on', options_sha256='on-options')
        for row in on:
            row['game_id'] += '-on'
            row['comparison_id'] += '-on'
            row['value'] = '100' if row['arm'] == 'candidate' else '0'
        _, absolute, _ = search.responses(rows + on, require_rotations=True)
        candidate = search.fitness(absolute)['candidate']
        self.assertEqual(candidate['objectives'], {})
        self.assertEqual({values['win_score'] for values in candidate['rule_objectives'].values()}, {'4', '100'})
        self.assertEqual(len(search.map_elites(absolute)), 2)
        off_key, on_key = sorted(candidate['rule_objectives'])
        self.assertFalse(search.dominates({off_key + '::win_score': 4}, {on_key + '::win_score': 1}))

    def test_game_identity_prevents_unrelated_control_pooling(self):
        rows = []
        for game, candidate, control in (('g1', 'a', 0), ('g2', 'b', 1)):
            for arm, value in ((candidate, 4 if candidate == 'a' else 7), ('default', control)):
                rows.append(dict(id='same-context', game_id=game, seed='1', metric='win_score',
                                 minute='-1', phase='all', arm=arm, value=str(value), status='measured'))
        values, _, _ = search.responses(rows)
        self.assertEqual(next(v['1'] for k, v in values.items() if k[0] == 'a'), 4)
        self.assertEqual(next(v['1'] for k, v in values.items() if k[0] == 'b'), 6)

    def test_filler_exports_complete_immutable_four_game_cohorts(self):
        plan = search.screen_plan(self.registry, self.genes[:1], fraction=False)
        plan['protocol'] = dict(contexts=[dict(mod='vanilla', difficulty='hard', size=96,
                                              base='cy', spice='medium', research='off')],
                                seeds=[1], minutes=30, modes=['1v1'], phase_minutes=[0, 10, 20],
                                houses=['harkonnen', 'atreides'])
        plan['plan_sha256'] = search.digest(plan)
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(rev='1' * 40, plan=Path(directory) / 'plan.json.gz',
                                      output=Path(directory) / 'results', tag='contract',
                                      emit_backlog=Path(directory) / 'backlog.json')
            search.save_json(args.plan, plan)
            exported = search.emit_backlog(plan, args, self.registry)
            self.assertEqual(len(exported['entries']), 1)
            entry = exported['entries'][0]
            self.assertEqual(entry['games'], 4)
            self.assertEqual(entry['id'], search.digest(dict(revision=args.rev, command=entry['command'])))
            self.assertEqual(entry['plan_sha256'], plan['plan_sha256'])

    def test_game_rules_follow_dependent_genes_and_are_never_searched_alone(self):
        names = [gene['id'] for gene in search.selected_genes(self.registry)]
        self.assertNotIn('technology_installments', names)
        self.assertIn('continuous_research', names)
        with self.assertRaisesRegex(ValueError, 'Game rules'):
            search.selected_genes(self.registry, ['technology_installments'])
        genes = search.selected_genes(self.registry, ['focus_fire', 'converge', 'continuous_research'])
        plan = search.screen_plan(self.registry, genes, parameter_sweeps=False)
        one = {d['gene']: d for d in plan['designs'] if d['stage'] == 'one_gene'}
        self.assertEqual(one['continuous_research']['game_rules'], {'technology_installments': 1})
        self.assertEqual(one['focus_fire']['game_rules'], {})
        genomes = {g['id']: g for g in plan['genomes']}
        self.assertEqual(genomes[one['continuous_research']['genome']]['values'],
                         {'continuous_research': 1, 'technology_installments': 1})
        self.assertEqual(plan['one_gene_only'], ['continuous_research'])
        self.assertNotIn('continuous_research', plan['fractional_columns'])
        for design in plan['designs']:
            if design['stage'] == 'fractional':
                self.assertNotIn('technology_installments', genomes[design['genome']]['values'])
        pairs = search.interaction_plan(self.registry, ['focus_fire', 'continuous_research'])
        for item in pairs['genomes'][1:]:
            self.assertEqual(item['values']['technology_installments'], 1)

    def test_operators_close_game_rules_over_active_dependents(self):
        genes = search.selected_genes(self.registry, ['focus_fire', 'continuous_research'])
        active = search.finish(search.genome('x', {'continuous_research': 1}), self.registry)
        self.assertEqual(active['values']['technology_installments'], 1)
        dropped = search.finish(search.genome('y', {'technology_installments': 1, 'focus_fire': 0}), self.registry)
        self.assertNotIn('technology_installments', dropped['values'])
        for seed in range(40):
            rng = random.Random(seed)
            child = search.crossover(active, dropped, genes, rng, self.registry)
            child, _ = search.mutate(child, genes, rng, registry=self.registry)
            genome_model.validate_genome(child, self.registry)
            needed = child['values'].get('continuous_research') == 1 or any(
                rule['values'].get('continuous_research') == 1 for rule in child['expression'])
            self.assertEqual(child['values'].get('technology_installments') == 1, needed)

    def test_evolution_seeds_new_genes_and_pairs_every_child_with_default(self):
        plan = search.screen_plan(self.registry, self.genes[:2], parameter_sweeps=False, fraction=False)
        names = [g['id'] for g in plan['genomes']]
        report = dict(pareto_fronts=[names[1:], [names[0]]], genomes={}, niches={})
        result = search.evolve(plan, report, self.registry, 1, population=4, offspring=6,
                               seed_genes=['continuous_research'], seeds=[7, 8])
        seeded = [r for r in result['lineage'] if r['role'] == 'seeded_gene']
        self.assertEqual(len(seeded), 1)
        child = next(g for g in result['genomes'] if g['id'] == seeded[0]['genome'])
        self.assertEqual(child['values']['technology_installments'], 1)
        self.assertEqual(result['protocol']['seeds'], [7, 8])
        for record in result['lineage']:
            self.assertIn([record['genome'], search.CONTROL], result['matchups'])
        expected = {r['genome'] for r in result['lineage']} | (set(result['parents']) - {search.CONTROL})
        self.assertEqual({d['genome'] for d in result['designs']}, expected)
        for parent in set(result['parents']) - {search.CONTROL}:
            self.assertIn([parent, search.CONTROL], result['matchups'])
        verdicts = {genome_model.genome_id(g): 'infeasible' for g in plan['genomes'] if g['id'] == names[1]}
        ranked = search.evolve(plan, report, self.registry, 1, population=4, offspring=2, feasibility=verdicts)
        self.assertEqual(ranked['feasibility'][names[1]], 'infeasible')
        self.assertNotEqual(ranked['parents'][0], names[1])

    def test_league_games_without_default_are_payoffs_not_fitness(self):
        rows = []
        for game, arms in (('g1', (('a', '1'), ('default', '0'))), ('g2', (('a', '0'), ('champion', '1')))):
            for arm, value in arms:
                rows.append(dict(id=game, game_id=game, seed='1', metric='win_score', minute='-1',
                                 phase='all', format='1v1', arm=arm, value=value, status='measured'))
        _, absolute, _ = search.responses(rows)
        self.assertEqual({k[0]: v for k, v in absolute.items()}, {'a': {'1': 1}, 'default': {'1': 0}})
        payoffs = search.payoff_table(rows)
        self.assertEqual(payoffs['champion'], {'games': 1, 'losses': 1})
        self.assertEqual(payoffs['default'], {'games': 1, 'losses': 0})
        self.assertEqual(payoffs['a'], {'games': 2, 'losses': 1})

    def test_native_pooling_needs_harness_only_git_differences(self):
        with patch.object(search.subprocess, 'check_output',
                          return_value='tests/ai/genome-match.py\ndocs/ai-genome.md\n'):
            proof = search.native_equivalence(['a' * 40, 'b' * 40])
        self.assertEqual(proof['differing_paths']['b' * 40], ['tests/ai/genome-match.py', 'docs/ai-genome.md'])
        with patch.object(search.subprocess, 'check_output', return_value='src/players/QuantBot.cpp\n'):
            with self.assertRaisesRegex(ValueError, 'outside the genome harness'):
                search.native_equivalence(['a' * 40, 'b' * 40])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'observations-x.csv'
            path.write_text('schema_version,source_commit,status\n1,%s,measured\n1,%s,measured\n'
                            % ('a' * 40, 'b' * 40))
            with self.assertRaisesRegex(ValueError, 'Do not pool'):
                list(search.observations([path]))
            self.assertEqual(len(list(search.observations([path], ['a' * 40, 'b' * 40]))), 2)

    def test_old_shards_recover_only_verified_original_assignment_slots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = self.rotation_rows()
            assignments = [dict(id=row['game_id'], house=row['house'], slot=row.pop('slot'),
                                genome_digest=row['genome_digest']) for row in rows]
            path = root / 'observations-old.csv.gz'
            search.write_rows(path, tuple(rows[0]), rows)
            search.write_rows(root / 'assignments.csv', tuple(assignments[0]), assignments)
            recovered = list(search.observations([root]))
            self.assertTrue(all(row['slot'] for row in recovered))
            self.assertTrue(search.responses(recovered, require_rotations=True)[0])
            assignments[0]['genome_digest'] = 'wrong-genotype'
            search.write_rows(root / 'assignments.csv', tuple(assignments[0]), assignments)
            with self.assertRaisesRegex(ValueError, 'conflicting original house assignment'):
                list(search.observations([path]))

    def test_feasibility_is_exact_source_rule_bound_and_worst_retry_wins(self):
        cases = [dict(id=case, adapted=True, game_rules={'technology_installments': 0}) for case in ('a', 'b')]
        genomes = [search.genome('g%d' % index, dict(focus_fire=index % 2, converge=index // 2 % 2, bundle=index // 4))
                   for index in range(6)]
        identities = [genome_model.genome_id(item) for item in genomes]
        rows = [dict(case=case, genome_digest=identity, source_commit='1' * 40, technology_installments='off',
                     completed='1', telemetry='complete', feasibility='feasible', error='')
                for identity in identities for case in ('a', 'b')]
        rows[3]['feasibility'] = 'infeasible'
        rows.append(dict(rows[3], feasibility='feasible'))
        rows[5]['error'] = 'rc=1'
        rows[6]['source_commit'] = rows[7]['source_commit'] = '2' * 40
        rows[9]['technology_installments'] = 'on'
        rows[11]['telemetry'] = 'capped'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.csv'
            search.write_rows(path, tuple(rows[0]), rows)
            with self.assertRaisesRegex(ValueError, 'evaluation-rev'):
                search.feasibility_table([path], cases, genomes=genomes)
            table = search.feasibility_table([path], cases, source='1' * 40, genomes=genomes)
        self.assertEqual([table[identity] for identity in identities],
                         ['feasible', 'infeasible', 'unverified', 'unverified', 'unverified', 'unverified'])

    def test_races_reject_all_ancestor_selection_seeds(self):
        plan = search.screen_plan(self.registry, self.genes[:1], parameter_sweeps=False, fraction=False)
        plan['protocol'] = dict(seeds=[1, 2], contexts=[], modes=['1v1'])
        elite = plan['genomes'][1]['id']
        with self.assertRaisesRegex(ValueError, 'eight seeds'):
            search.race_plan([plan], [elite], [2, 3, 4, 5, 6, 7, 8, 9], self.registry)
        race = search.race_plan([plan], [elite], list(range(10, 18)), self.registry)
        self.assertEqual(race['matchups'], [[elite, search.CONTROL]])
        self.assertEqual(race['designs'], [dict(stage='candidate', genome=elite)])
        plan['selection_seeds'] = [91, 92]
        with self.assertRaisesRegex(ValueError, 'eight seeds'):
            search.race_plan([plan], [elite], [91, 10, 11, 12, 13, 14, 15, 16], self.registry)
        report = dict(pareto_fronts=[[g['id'] for g in plan['genomes']]], genomes={}, niches={})
        evolved = search.evolve(plan, report, self.registry, 1, population=4, offspring=1, seeds=[3, 4])
        self.assertEqual(evolved['selection_seeds'], [1, 2, 3, 4, 91, 92])

    def test_joint_seed_is_measured_without_fake_fitness_or_extra_offspring(self):
        plan = search.screen_plan(self.registry, self.genes[:1], parameter_sweeps=False, fraction=False)
        report = dict(pareto_fronts=[[g['id'] for g in plan['genomes']]], genomes={}, niches={})
        genes = ['existing_composition_unit_choice', 'buffered_heavy_production', 'income_heavy_factories']
        result = search.evolve(plan, report, self.registry, 1, population=4, offspring=6,
                               seed_genes=genes, seed_combinations=[genes])
        self.assertEqual(len(result['lineage']), 6)
        joint = next(row for row in result['lineage'] if row['role'] == 'seeded_combination')
        child = next(item for item in result['genomes'] if item['id'] == joint['genome'])
        self.assertTrue(all(child['values'][gene] == 1 for gene in genes))
        self.assertIn([child['id'], search.CONTROL], result['matchups'])
        self.assertEqual({row['mutations'][0] for row in result['lineage'] if row['role'] == 'seeded_gene'},
                         set(genes))
        with self.assertRaisesRegex(ValueError, 'offspring count'):
            search.evolve(plan, report, self.registry, 1, population=4, offspring=2,
                          seed_genes=genes, seed_combinations=[genes])

    def test_race_context_expansion_retains_frozen_size_base_spice_and_seed_safety(self):
        plan = search.screen_plan(self.registry, self.genes[:1], parameter_sweeps=False, fraction=False)
        plan['protocol'] = dict(seeds=[1, 2], contexts=[dict(mod='dunecity', difficulty='hard', research='off',
                                                          size=96, base='cy', spice='medium')], modes=['1v1'])
        elite = plan['genomes'][1]['id']
        race = search.race_plan([plan], [elite], list(range(10, 18)), self.registry,
                                ['dunecity:brutal:on', 'vanilla:hard:off'])
        self.assertEqual({(row['mod'], row['difficulty'], row['research']) for row in race['protocol']['contexts']},
                         {('dunecity', 'brutal', 'on'), ('vanilla', 'hard', 'off')})
        self.assertTrue(all(row['size'] == 96 and row['base'] == 'cy' and row['spice'] == 'medium'
                            for row in race['protocol']['contexts']))
        self.assertEqual(plan['protocol']['contexts'][0]['difficulty'], 'hard')
        with self.assertRaisesRegex(ValueError, 'Race contexts'):
            search.race_plan([plan], [elite], list(range(10, 18)), self.registry, ['unknown:hard:off'])


if __name__ == '__main__':
    unittest.main()
