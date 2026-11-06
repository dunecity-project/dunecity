"""Synthetic search/statistics contracts; never evidence about AI game strength."""
import argparse
import copy
from fractions import Fraction
import random
import unittest
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
        rows = []
        for game in range(4):
            for arm, value in (('candidate', '4'), ('default', '0')):
                rows.append(dict(id='shared-context', game_id=str(game), comparison_id='pair',
                                 seed='1', mod='dunecity', difficulty='hard', research='off', format='1v1',
                                 size='96', phase='all', base='cy', spice_level='medium', minute='-1',
                                 metric='win_score', arm=arm, value=value, status='measured'))
        incomplete, _, _ = search.responses(rows[:-2], require_rotations=True)
        self.assertFalse(incomplete)
        complete, _, _ = search.responses(rows, require_rotations=True)
        self.assertEqual(next(iter(complete.values())), {'1': 4})
        self.assertEqual(next(iter(complete))[2][-1], 'medium')

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


if __name__ == '__main__':
    unittest.main()
