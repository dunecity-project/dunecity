"""Pure-Python genome contracts; no build, simulation, or AI outcome claims."""
import configparser
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

import genome_model as model


CONTEXT = {'difficulty': 'hard', 'mod': 'dunecity', 'house': 'atreides',
           'format': '1v1', 'size': 96, 'phase': 'opening'}


def context(**changes):
    return dict(CONTEXT, **changes)


def genome(values=None, expression=None):
    return {'id': 'candidate-1', 'values': {} if values is None else values,
            'expression': [] if expression is None else expression}


def ini_values(text):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read_string(text)
    return {(section, key): value for section in ini.sections() for key, value in ini.items(section)}


class GenomeModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.registry = model.load_registry()
        cls.genes = {gene['id']: gene for gene in cls.registry}
        cls.small = [cls.genes[key] for key in ('focus_fire', 'wave_readiness_percent', 'attack_timer_ms',
                                               'hard_attack_threshold_percent')]

    def test_full_registry_covers_every_real_boolean_and_enum(self):
        source = (model.ROOT / 'src/players/QuantBotConfig.cpp').read_text(encoding='utf-8')
        catalog = model._reader_catalog(source)
        registered = {(gene['section'], gene['key']): gene for gene in self.registry}
        for pair, (kind, _) in catalog.items():
            if kind in ('bool', 'enum'):
                with self.subTest(pair=pair):
                    self.assertIn(pair, registered)
                    self.assertEqual(registered[pair]['kind'], kind)
        self.assertGreaterEqual(sum(gene['kind'] == 'bool' for gene in self.registry), 61)
        self.assertEqual(len(registered), len(self.registry))
        for gene in self.registry:
            self.assertTrue(gene['owner_doc'])
            self.assertGreaterEqual(len(gene['levels']), 2)
            self.assertIn(gene['default'], gene['levels'])

    def test_forgotten_boolean_is_not_silently_omitted(self):
        source = (model.ROOT / 'src/players/QuantBotConfig.cpp').read_text(encoding='utf-8')
        marker = 'bool QuantBotConfig::readFrom(const INIFile& iniFile) {'
        self.assertIn(marker, source)
        changed = source.replace(marker, marker + '\n    force.futureFlag = iniFile.getBoolValue(\n'
                                 '        "Force Concentration", "FutureFlag", force.futureFlag);', 1)
        catalog = model._reader_catalog(changed)
        self.assertEqual(catalog[('Force Concentration', 'FutureFlag')][0], 'bool')
        _, defaults, mixes, tempos = model._source_catalog()
        with patch.object(model, '_source_catalog', return_value=(catalog, defaults, mixes, tempos)):
            with self.assertRaisesRegex(ValueError, 'missing boolean.*FutureFlag'):
                model._validate_registry(self.registry, complete=True)

    def test_missing_existing_boolean_or_enum_rejects_full_registry(self):
        for gene_id in ('focus_fire', 'hard_attack_enabled', 'wave_formation', 'strategy_mode'):
            with self.subTest(gene_id=gene_id):
                reduced = [gene for gene in self.registry if gene['id'] != gene_id]
                with self.assertRaisesRegex(ValueError, 'missing (boolean|enum)'):
                    model._validate_registry(reduced, complete=True)

    def test_function_discovery_ignores_an_earlier_invocation_with_an_if_block(self):
        source = '''
bool QuantBotConfig::load(const std::string& filepath) {
    if (readFrom(iniFile) && !readOnly) {
        save(filepath);
    }
    return true;
}
bool QuantBotConfig::readFrom(
        const INIFile& iniFile) {
    loadDifficultySettings(iniFile, "Difficulty Settings", "Hard", hard);
    return false;
}
'''
        body = model._function(source, 'readFrom')
        self.assertIn('loadDifficultySettings', body)
        self.assertIn('return false;', body)
        self.assertNotIn('save(filepath)', body)
        self.assertNotIn('return true;', body)
        self.assertEqual(model._function('Settings::Settings() { random = false; }', 'Settings'),
                         ' random = false; ')
        self.assertEqual(model._function('static void loadOne(\\n int value) { use(value); }', 'loadOne'),
                         ' use(value); ')

    def test_reader_inventory_expands_helpers_and_string_fallback_functions(self):
        catalog, _, _, _ = model._source_catalog()
        self.assertEqual(catalog[('Difficulty Settings', 'Defend_AttackEnabled')][0], 'bool')
        self.assertEqual(catalog[('Difficulty Settings', 'Brutal_OrnithopterAttackEnabled')][0], 'bool')
        self.assertEqual(catalog[('Army Recovery', 'WithdrawalsEnabled')][0], 'bool')
        self.assertEqual(catalog[('General Behavior', 'WaveFormation')][0], 'enum')
        self.assertEqual(catalog[('Unit Ratios Tuned', 'Mercenary_Tech9')][0], 'ratios')
        self.assertEqual(catalog[('Difficulty Settings', 'Hard_HarvesterLimitMultiplier')][0], 'int')

    def test_effective_defaults_follow_ini_then_constructor_without_float_math(self):
        values = model.resolve(genome(), CONTEXT, self.registry)
        expected = {
            'attack_timer_ms': 90000,
            'attack_threshold_percent': '0.40',
            'hard_harvester_limit_multiplier': 2,
            'hard_military_value_multiplier': '3.0',
            'medium_military_value_multiplier': '2.5',
            'brutal_harvester_limit_multiplier': 999,
            'brutal_refinery_minimum': 4,
            'unit_ratio_ordos_launcher': '0.0',
            'recovery_enabled': 1,
            'recovery_withdrawals_enabled': 0,
            'strategy_mode': 'Fixed:Tuned,Swarm',
            'tuned_ratio_harkonnen': '14,16,15,22,0,0,0,0,0,33',
            'tuned_ratio_fremen_tech2': '0,0,0,0,0,0,0,50,50,0',
            'tuned_ratio_sardaukar_tech2': '',
        }
        for gene_id, value in expected.items():
            with self.subTest(gene_id=gene_id):
                self.assertEqual(values[gene_id], value)
                self.assertIs(type(values[gene_id]), type(value))
        self.assertTrue(all(type(value) in (int, str) for value in values.values()))

    def test_default_genome_is_sparse_independent_and_native_baseline_preserving(self):
        first, second = model.default_genome(self.registry), model.default_genome(self.registry)
        self.assertEqual(first, {'id': 'default', 'values': {}, 'expression': []})
        self.assertEqual(model.render_ini(first, CONTEXT, self.registry), '')
        first['values']['focus_fire'] = 0
        first['expression'].append({'values': {'wave_readiness_percent': 45}})
        self.assertEqual(second['values'], {})
        self.assertEqual(second['expression'], [])
        self.assertEqual(self.genes['focus_fire']['default'], 1)

    def test_explicit_registry_subsets_are_supported(self):
        candidate = genome({'focus_fire': 0})
        model.validate_genome(candidate, self.small)
        resolved = model.resolve(candidate, CONTEXT, self.small)
        self.assertEqual(set(resolved), {gene['id'] for gene in self.small})
        self.assertEqual(resolved['focus_fire'], 0)
        self.assertEqual(resolved['attack_timer_ms'], 90000)
        self.assertEqual(model.render_ini(candidate, CONTEXT, self.small), '[Force Concentration]\nFocusFire=0\n')
        with self.assertRaisesRegex(ValueError, 'unknown gene'):
            model.validate_genome(genome({'converge': 0}), self.small)

    def test_all_context_dimensions_and_inclusive_map_size_bounds(self):
        when = {'difficulty': ['hard'], 'mod': ['dunecity'], 'house': ['atreides'],
                'format': ['1v1'], 'size_min': 96, 'size_max': 128, 'phase': ['opening', 'mid']}
        candidate = genome(expression=[{'when': when, 'values': {'focus_fire': 0}}])
        for size in (96, 128):
            self.assertEqual(model.resolve(candidate, context(size=size), self.small)['focus_fire'], 0)
        for changes in ({'difficulty': 'medium'}, {'mod': 'vanilla'}, {'house': 'ordos'},
                        {'format': 'ffa'}, {'size': 95}, {'size': 129}, {'phase': 'late'}):
            with self.subTest(changes=changes):
                self.assertEqual(model.resolve(candidate, context(**changes), self.small)['focus_fire'], 1)
        self.assertEqual(model.resolve(candidate, context(phase='mid'), self.small)['focus_fire'], 0)

    def test_missing_and_empty_when_dimensions_match_all(self):
        rules = [
            {'when': {key: [] for key in ('difficulty', 'mod', 'house', 'format', 'phase')},
             'values': {'focus_fire': 0}},
            {'when': {}, 'values': {'attack_timer_ms': 30000}},
            {'values': {'wave_readiness_percent': 45}},
        ]
        result = model.resolve(genome(expression=rules), context(mod='another-mod', house='rebels', format='3v3'), self.small)
        self.assertEqual((result['focus_fire'], result['attack_timer_ms'], result['wave_readiness_percent']), (0, 30000, 45))

    def test_overlapping_rules_apply_in_order_and_only_replace_assigned_genes(self):
        rules = [
            {'values': {'focus_fire': 0, 'wave_readiness_percent': 45}},
            {'when': {'difficulty': ['hard']}, 'values': {'focus_fire': 1}},
            {'when': {'phase': ['late']}, 'values': {'focus_fire': 0}},
        ]
        candidate = genome({'focus_fire': 1}, rules)
        model.validate_genome(candidate, self.small)
        result = model.resolve(candidate, CONTEXT, self.small)
        self.assertEqual((result['focus_fire'], result['wave_readiness_percent']), (1, 45))
        candidate['expression'] = list(reversed(rules))
        self.assertEqual(model.resolve(candidate, CONTEXT, self.small)['focus_fire'], 0)

    def test_phase_resolution_has_no_state_or_input_mutation(self):
        candidate = genome(expression=[{'when': {'phase': ['opening']}, 'values': {'focus_fire': 0}}])
        original, original_context = deepcopy(candidate), deepcopy(CONTEXT)
        opening = model.resolve(candidate, CONTEXT, self.small)
        opening['focus_fire'] = 1
        self.assertEqual(model.resolve(candidate, CONTEXT, self.small)['focus_fire'], 0)
        self.assertEqual(model.resolve(candidate, context(phase='mid'), self.small)['focus_fire'], 1)
        self.assertEqual(model.render_ini(candidate, context(phase='late'), self.small), '')
        self.assertEqual(candidate, original)
        self.assertEqual(CONTEXT, original_context)

    def test_rendering_uses_only_real_grouped_ini_keys_and_exact_decimal_strings(self):
        candidate = genome({'focus_fire': 0, 'attack_timer_ms': 30000,
                            'hard_attack_threshold_percent': '0.20', 'strategy_mode': 'Fixed:Artillery,Base',
                            'tuned_ratio_atreides_tech4': 'Opening'})
        text = model.render_ini(candidate, CONTEXT, self.registry)
        self.assertEqual(ini_values(text), {
            ('Force Concentration', 'FocusFire'): '0',
            ('General Behavior', 'AttackTimerMs'): '30000',
            ('Difficulty Settings', 'Hard_AttackThresholdPercent'): '0.20',
            ('Strategy', 'Mode'): 'Fixed:Artillery,Base',
            ('Unit Ratios Tuned', 'Atreides_Tech4'): 'Opening',
        })
        self.assertEqual(text, model.render_ini(candidate, CONTEXT, list(reversed(self.registry))))
        self.assertNotIn('[Play Style]', text)
        self.assertNotIn('Slab1_Build', text)
        self.assertTrue(text.endswith('\n'))

    def test_explicit_default_and_empty_ratio_levels_are_not_ignored(self):
        candidate = genome({'focus_fire': 1, 'tuned_ratio_atreides': ''})
        self.assertEqual(ini_values(model.render_ini(candidate, CONTEXT, self.registry)), {
            ('Force Concentration', 'FocusFire'): '1', ('Unit Ratios Tuned', 'Atreides'): '',
        })

    def test_unknown_or_malformed_genomes_fail_even_for_unmatched_rules(self):
        invalid = [
            {}, {'id': '../escape'}, {'id': 'Uppercase'}, {'id': 'has space'},
            {'id': 'valid', 'unknown': 1}, genome(values=[]), genome({'not_a_gene': 0}),
            genome({'focus_fire': 2}), genome({'focus_fire': '1'}), genome({'focus_fire': True}),
            genome({'focus_fire': 1.0}), genome({'hard_attack_threshold_percent': 0.20}),
            genome({'hard_attack_threshold_percent': 1}), genome(expression={}),
            genome(expression=[{'values': {}, 'unknown': 1}]), genome(expression=[{'when': {}}]),
            genome(expression=[{'when': [], 'values': {}}]),
            genome(expression=[{'when': {'phases': ['late']}, 'values': {}}]),
            genome(expression=[{'when': {'difficulty': ['impossible']}, 'values': {}}]),
            genome(expression=[{'when': {'phase': ['endgame']}, 'values': {}}]),
            genome(expression=[{'when': {'mod': 'dunecity'}, 'values': {}}]),
            genome(expression=[{'when': {'house': None}, 'values': {}}]),
            genome(expression=[{'when': {'format': ['1v1', '1v1']}, 'values': {}}]),
            genome(expression=[{'when': {'size_min': True}, 'values': {}}]),
            genome(expression=[{'when': {'size_min': -1}, 'values': {}}]),
            genome(expression=[{'when': {'size_max': 96.0}, 'values': {}}]),
            genome(expression=[{'when': {'size_min': 128, 'size_max': 96}, 'values': {}}]),
            genome(expression=[{'when': {'phase': ['late']}, 'values': {'unknown_gene': 0}}]),
            genome(expression=[{'when': {'phase': ['late']}, 'values': {'focus_fire': 99}}]),
        ]
        for candidate in invalid:
            with self.subTest(candidate=candidate):
                with self.assertRaises(ValueError):
                    model.validate_genome(candidate, self.registry)

    def test_context_must_be_complete_and_typed(self):
        invalid = [{}, dict(CONTEXT, unknown=1), context(size=True), context(size=0), context(size=96.0),
                   context(phase='unknown'), context(difficulty='Hard'), context(mod=''), context(house=1)]
        for value in invalid:
            with self.subTest(context=value):
                with self.assertRaises(ValueError):
                    model.resolve(genome(), value, self.small)

    def test_registry_rejects_unknown_keys_types_defaults_levels_and_clamping(self):
        changes = [
            ('focus_fire', {'key': 'ForgottenKey'}),
            ('focus_fire', {'section': 'Game Options'}),
            ('focus_fire', {'kind': 'int'}),
            ('focus_fire', {'levels': [0, 1, True]}),
            ('focus_fire', {'levels': [0, 1, 2]}),
            ('focus_fire', {'levels': [0, 1, 1]}),
            ('focus_fire', {'default': 0}),
            ('focus_fire', {'owner_doc': ''}),
            ('focus_fire', {'unknown': 1}),
            ('wave_readiness_percent', {'levels': [9, 60]}),
            ('wave_readiness_percent', {'levels': [60, 301]}),
            ('launcher_kite_extra_distance', {'levels': [0, 2, 3]}),
            ('hard_harvester_limit_multiplier', {'levels': [1, 2, '2.5']}),
            ('hard_attack_threshold_percent', {'levels': ['0.30', 0.40]}),
            ('hard_attack_threshold_percent', {'levels': ['0.30', 'NaN']}),
            ('wave_formation', {'levels': ['None', 'RemovedShape']}),
            ('strategy_mode', {'levels': ['Fixed:Tuned,Swarm', 'Fixed:Unknown,Swarm']}),
            ('strategy_mix_weights', {'levels': [self.genes['strategy_mix_weights']['default'], 'Unknown:1']}),
            ('strategy_tempo_weights', {'levels': [self.genes['strategy_tempo_weights']['default'], 'Swarm:0']}),
            ('strategy_tempo_weights', {'levels': [self.genes['strategy_tempo_weights']['default'], 'Swarm:1,Swarm:2']}),
            ('tuned_ratio_atreides', {'levels': [self.genes['tuned_ratio_atreides']['default'], 'Opening']}),
            ('tuned_ratio_atreides', {'levels': [self.genes['tuned_ratio_atreides']['default'], '0,0,0,0,0,0,0,0,0,0']}),
            ('tuned_ratio_atreides', {'levels': [self.genes['tuned_ratio_atreides']['default'], '1,2,3']}),
            ('wave_formation', {'levels': ['None', 'Box\n[Game Options]']}),
        ]
        for gene_id, update in changes:
            with self.subTest(gene_id=gene_id, update=update):
                gene = dict(self.genes[gene_id], **update)
                with self.assertRaises(ValueError):
                    model._validate_registry([gene])
        for duplicated in ([self.genes['focus_fire']] * 2,
                           [self.genes['focus_fire'], dict(self.genes['focus_fire'], id='other_id')]):
            with self.assertRaises(ValueError):
                model._validate_registry(duplicated)

    def test_strategy_and_tuned_levels_cover_current_modes_and_opening_switches(self):
        _, _, mixes, tempos = model._source_catalog()
        self.assertEqual(set(self.genes['strategy_mode']['levels']),
                         {'Random'} | {'Fixed:' + mix + ',' + tempo for mix in mixes for tempo in tempos})
        tuned = [gene for gene in self.registry if gene['section'] == 'Unit Ratios Tuned']
        self.assertEqual(len(tuned), 60)
        for gene in tuned:
            self.assertEqual('Opening' in gene['levels'], '_Tech' in gene['key'])
        self.assertNotEqual(self.genes['recovery_enabled']['id'], self.genes['recovery_attrition_window_ms']['id'])
        self.assertNotEqual(self.genes['hard_attack_enabled']['id'], self.genes['hard_attack_threshold_percent']['id'])

    def test_registry_file_loading_rejects_duplicates_json_floats_and_missing_genes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'registry.json'
            path.write_text(json.dumps(self.registry), encoding='utf-8')
            self.assertEqual(model.load_registry(path), self.registry)
            for content in ('[{"id":"a","id":"b"}]', '[1.0]', '[NaN]', '{}', '[]',
                            json.dumps([gene for gene in self.registry if gene['id'] != 'focus_fire'])):
                with self.subTest(content=content[:80]):
                    path.write_text(content, encoding='utf-8')
                    with self.assertRaises(ValueError):
                        model.load_registry(path)

    def test_identity_is_canonical_registry_independent_and_not_a_human_label(self):
        first = genome({'focus_fire': 0, 'wave_readiness_percent': 45}, [
            {'when': {'difficulty': ['hard', 'brutal'], 'phase': []}, 'values': {'focus_fire': 1}},
        ])
        second = {'expression': [{'values': {'focus_fire': 1}, 'when': {'difficulty': ['brutal', 'hard']}}],
                  'values': {'wave_readiness_percent': 45, 'focus_fire': 0}, 'id': 'different-label'}
        digest = model.genome_id(first)
        self.assertRegex(digest, r'^[0-9a-f]{64}$')
        self.assertEqual(digest, model.genome_id(second))
        canonical = {'values': first['values'], 'expression': [
            {'when': {'difficulty': ['brutal', 'hard']}, 'values': {'focus_fire': 1}}]}
        self.assertEqual(digest, hashlib.sha256(json.dumps(canonical, sort_keys=True, separators=(',', ':'),
                                                         ensure_ascii=True).encode('utf-8')).hexdigest())
        second['values']['focus_fire'] = 1
        self.assertNotEqual(digest, model.genome_id(second))
        self.assertEqual(model.genome_id({'id': 'a'}), model.genome_id(genome()))

    def test_identity_preserves_expression_order(self):
        candidate = genome(expression=[{'values': {'focus_fire': 0}}, {'values': {'focus_fire': 1}}])
        forward = model.genome_id(candidate)
        candidate['expression'].reverse()
        self.assertNotEqual(forward, model.genome_id(candidate))
        self.assertNotEqual(model.genome_id(candidate), model.genome_id(genome({'focus_fire': 1})))

    def dependent_registry(self):
        # Exercise dependency machinery without inventing a shipped dependency.
        return model._validate_registry([
            dict(gene, requires={'technology_installments': 1})
            if gene['id'] == 'simple_tech_savings' else gene for gene in self.registry])

    def test_game_rule_genes_audit_the_real_game_options_reader_and_default(self):
        rule = self.genes['technology_installments']
        self.assertEqual((rule['scope'], rule['section'], rule['key'], rule['default']),
                         ('game', 'Game Options', 'Technology Installments', 0))
        self.assertIs(model._game_option_catalog()['Technology Installments'], 0)
        self.assertTrue(all('requires' not in gene for gene in self.registry))
        for update in ({'key': 'Invented Rule'}, {'section': 'Force Concentration'}, {'default': 1},
                       {'kind': 'int'}, {'levels': [0, 2]}, {'scope': 'house'}, {'difficulty': ['hard']},
                       {'requires': {'technology_installments': 1}}):
            with self.subTest(update=update):
                with self.assertRaises(ValueError):
                    model._validate_registry([dict(rule, **update)])
        dependent = next(gene for gene in self.dependent_registry() if gene['id'] == 'simple_tech_savings')
        for requires in ({'focus_fire': 1}, {'technology_installments': 0}, {'technology_installments': 2},
                         {'missing_rule': 1}, {}):
            with self.subTest(requires=requires):
                with self.assertRaises(ValueError):
                    model._validate_registry([dict(dependent, requires=requires), rule, self.genes['focus_fire']])

    def test_dependent_gene_needs_its_whole_game_rule_and_rules_are_never_per_house(self):
        registry = self.dependent_registry()
        with self.assertRaisesRegex(ValueError, 'requires game rule technology_installments=1'):
            model.validate_genome(genome({'simple_tech_savings': 1}), registry)
        with self.assertRaisesRegex(ValueError, 'requires game rule'):
            model.validate_genome(genome(expression=[{'when': {'mod': ['dunecity']},
                                                      'values': {'simple_tech_savings': 1}}]), registry)
        with self.assertRaisesRegex(ValueError, 'whole-game top-level'):
            model.validate_genome(genome({'simple_tech_savings': 1}, [
                {'when': {'phase': ['late']}, 'values': {'technology_installments': 1}}]), registry)
        candidate = genome({'simple_tech_savings': 1, 'technology_installments': 1})
        model.validate_genome(candidate, registry)
        model.validate_genome(genome({'simple_tech_savings': 0}), registry)
        rendered = ini_values(model.render_ini(candidate, CONTEXT, registry))
        self.assertEqual(rendered, {('Force Concentration', 'SimpleTechSavings'): '1'})
        self.assertEqual(model.resolve(candidate, CONTEXT, registry)['technology_installments'], 1)

    def test_roster_game_rules_are_explicit_shared_and_conflicts_fail(self):
        registry = self.dependent_registry()
        candidate = genome({'simple_tech_savings': 1, 'technology_installments': 1})
        default = {'id': 'default', 'values': {}, 'expression': []}
        self.assertEqual(model.game_rules([candidate, default], registry), {'technology_installments': 1})
        self.assertEqual(model.game_rules([default, default], registry), {'technology_installments': 0})
        explicit_off = {'id': 'off', 'values': {'technology_installments': 0}, 'expression': []}
        with self.assertRaisesRegex(ValueError, 'conflicting whole-game rule'):
            model.game_rules([candidate, explicit_off], registry)
        closed = model.close_game_rules(genome({'simple_tech_savings': 1}), registry)
        self.assertEqual(closed['values'], {'simple_tech_savings': 1, 'technology_installments': 1})
        self.assertEqual(model.close_game_rules(explicit_off, registry)['values'], {})


if __name__ == '__main__':
    unittest.main()
