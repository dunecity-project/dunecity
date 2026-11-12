"""Parser/measurement contracts only; synthetic rows are not native AI evidence."""
import ast
import base64
import configparser
import gzip
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HARNESS = Path(__file__).with_name('genome-behaviors.py')
body = []
for node in ast.parse(HARNESS.read_text(), str(HARNESS)).body:
    if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'args' for t in node.targets):
        break
    body.append(node)
else:
    raise AssertionError('missing cluster argument boundary')
ns = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=body, type_ignores=[]), str(HARNESS), 'exec'), ns)
runner = SimpleNamespace(**ns)


def event(name, data, cycle=110, house=2):
    return dict(session='s', house=house, cycle=cycle, event=name, data=data)


def sample(phase='loaded', house=2, **changes):
    row = dict(phase=phase, cycle=100 if phase == 'loaded' else 200, house=house, elapsed=0,
        bots=2, other_controllers=0, colour_slot=1, yard=1, light_factory=0, light_factory_sites=365,
        tech=2, research_target=0, research_remaining=0, research_start_legal=0,
        research_lab=0, rocket_turret_built=0)
    row.update(ready_yards=1, heavy_factory=0, heavy_factory_queued=0, heavy_factory_built=0,
        heavy_factory_available=1, heavy_factory_sites=4, heavy_factory_power=50, starport=1,
        light_factory_queued=0, light_factory_built=0, yard_built=0, residential_built=0,
        industrial_built=0, commercial_built=0, rocket_turret=0, unpowered_zones_vacate=0)
    row['yard_locations'] = {'10': {'x': 1, 'y': 1}}
    row.update(changes)
    return row


class BehaviorContracts(unittest.TestCase):
    def test_queue_boundary_and_tuple_schedule(self):
        for name in ('parser', 'schedule', 'build_probe', 'play', 'report', 'read', 'finish_game', 'scratch_dir'):
            self.assertIn(name, ns)
        args = runner.parser.parse_args(['--case', 'notbuilding'])
        self.assertEqual(runner.KEY, ('case', 'genome'))
        self.assertEqual(runner.schedule(args), [('notbuilding', 'default')])
        self.assertEqual((args.capture_mib, args.wall_timeout), (1024, 3600))

    def test_default_is_sparse_and_manifest_does_not_add_extra_games(self):
        genome = {'id': 'growth', 'values': {'city_growth_power': 0}}
        encoded = base64.b64encode(json.dumps({'schema_version': 1, 'genomes': [genome]}).encode()).decode()
        args = runner.parser.parse_args(['--case', 'ainonuclear', '--manifest-b64', encoded])
        self.assertEqual(runner.schedule(args), [('ainonuclear', 'growth')])
        self.assertEqual(runner.manifest(None)['default']['values'], {})

    def test_game_rule_dependency_is_validated(self):
        registry = [dict(gene, requires={'technology_installments': 1})
                    if gene['id'] == 'simple_tech_savings' else gene
                    for gene in runner.genome_model.load_registry()]
        genome = {'id': 'bad', 'values': {'simple_tech_savings': 1}}
        encoded = base64.b64encode(json.dumps({'schema_version': 1, 'genomes': [genome]}).encode()).decode()
        with patch.object(runner.genome_model, 'load_registry', return_value=registry):
            with self.assertRaisesRegex(ValueError, 'requires game rule'):
                runner.manifest(encoded)

    def test_all_ten_required_issues_have_native_adapters(self):
        cases = runner.definitions()
        self.assertEqual(len(cases), 10)
        self.assertTrue(all(c['adapted'] for c in cases.values()))
        for case in ('atreides_no_expand', 'purple_zero_power', 'sardaukar_lf', 'insufficient_rocket_turrets'):
            self.assertEqual(runner.schedule(runner.parser.parse_args(['--case', case])), [(case, 'default')])
        self.assertEqual(cases['insufficient_rocket_turrets']['save_sha256'],
                         '6ab7c6f736e84a8ba02ea03888c862845abacafdb9b0496b74588f27431cfee8')

    def check(self, kind, samples=None, events=None, setup=None):
        return runner.evaluate_check({'type': kind}, samples or [sample(), sample('finished')],
                                     events or [], setup or {}, {})

    def test_lf_requires_accepted_order_before_yard_loss(self):
        rows = [sample(), sample('change', cycle=120, yard=0), sample('finished', yard=0)]
        refused = event('production_order', {'item': 7, 'accepted': 0})
        self.assertEqual(self.check('light_factory_order', rows, [refused])[0], 'fail')
        accepted = event('production_order', {'item': 7, 'accepted': 1})
        self.assertEqual(self.check('light_factory_order', rows, [accepted])[0], 'pass')
        accepted['cycle'] = 121
        self.assertEqual(self.check('light_factory_order', rows, [accepted])[0], 'fail')
        rows[0]['light_factory_sites'] = 0
        self.assertEqual(self.check('light_factory_order', rows, [accepted])[0], 'unverified')

    def test_funded_hf_requires_first_native_plan_and_new_completion(self):
        rows = [sample(house=1, light_factory=1), sample('finished', house=1, light_factory=1, heavy_factory=1, heavy_factory_built=1)]
        plan = event('capital_plan', dict(mode='dunecity', campaign=0, defending=0, ready_yards=1,
            spendable=725, forecast_tax_per_minute=1457, forecast_upkeep_per_minute=129,
            power_produced=600, power_required=518, selected=0, candidates={'0': dict(
                item=4, price=600, foundation_cost=0, affordable=1, kind='production',
                reason='bootstrap_managed_separately', urgent=0, score=0)}), house=1)
        plan['seq'] = 7
        order = event('production_order', dict(item=4, accepted=1, quoted_price=600, capital_plan=8), 190, 1)
        self.assertEqual(self.check('heavy_factory_prompt', rows, [plan, order], {'concrete_required': 0})[0], 'fail')
        order['data']['capital_plan'] = 7
        self.assertEqual(self.check('heavy_factory_prompt', rows, [plan, order], {'concrete_required': 0})[0], 'pass')
        projected = [runner.project_event(e) for e in (plan, order)]
        self.assertEqual(self.check('heavy_factory_prompt', rows, projected, {'concrete_required': 0})[0], 'pass')
        rows[-1]['heavy_factory_built'] = 0
        self.assertEqual(self.check('heavy_factory_prompt', rows, projected, {'concrete_required': 0})[0], 'fail')
        plan['data']['spendable'] = 599
        self.assertEqual(self.check('heavy_factory_prompt', rows, [plan, order], {'concrete_required': 0})[0], 'unverified')

    def test_funded_hf_defers_real_urgent_air_coverage_not_cheap_imports(self):
        rows = [sample(house=1, light_factory=1), sample('finished', house=1, light_factory=1, heavy_factory=1, heavy_factory_built=1)]
        data = dict(mode='dunecity', campaign=0, defending=0, ready_yards=1, spendable=725,
            forecast_tax_per_minute=1457, forecast_upkeep_per_minute=129, power_produced=600,
            power_required=518, ground_threat_value=0, ground_turret_goal=0, ground_turret_cover=0)
        hf = dict(item=4, price=600, foundation_cost=0, affordable=1, kind='production',
                  reason='bootstrap_managed_separately', urgent=0, score=0)
        defence = dict(item=12, price=600, foundation_cost=0, affordable=1, kind='civic',
                       reason='air_coverage', urgent=1, score=3000)
        early = event('capital_plan', dict(data, selected=1, candidates={'0': hf, '1': defence}), house=1)
        early['seq'] = 7
        later = event('capital_plan', dict(data, selected=0, candidates={'0': hf}), 150, 1)
        later['seq'] = 8
        order = event('production_order', dict(item=4, accepted=1, quoted_price=600, capital_plan=8), 160, 1)
        projected = [runner.project_event(e) for e in (early, later, order)]
        self.assertEqual(projected[0]['data']['candidates']['1']['urgent'], 1)
        self.assertEqual(self.check('heavy_factory_prompt', rows, projected, {'concrete_required': 0})[0], 'pass')
        defence.update(reason='military_shortfall', urgent=0)
        projected = [runner.project_event(e) for e in (early, later, order)]
        self.assertEqual(self.check('heavy_factory_prompt', rows, projected, {'concrete_required': 0})[0], 'fail')

    def test_lf_completion_and_real_growth_are_independent(self):
        rows = [sample(house=4), sample('finished', house=4)]
        order = event('production_order', {'item': 7, 'accepted': 1}, house=4)
        self.assertEqual(self.check('light_factory_built', rows, [order])[0], 'fail')
        self.assertEqual(self.check('city_or_yard_growth', rows, [order])[0], 'fail')
        rows[-1].update(light_factory=1, light_factory_built=1)
        self.assertEqual(self.check('light_factory_built', rows, [order])[0], 'pass')
        self.assertEqual(self.check('city_or_yard_growth', rows, [order])[0], 'fail')
        rows[-1]['residential_built'] = 1
        self.assertEqual(self.check('city_or_yard_growth', rows)[0], 'pass')
        rows[-1].update(residential_built=0, yard_built=1)
        self.assertEqual(self.check('city_or_yard_growth', rows)[0], 'fail')
        rows[-1].update(yard=2, yard_built=0, yard_locations={'10': {'x': 1, 'y': 1}, '20': {'x': 8, 'y': 8}})
        self.assertEqual(self.check('city_or_yard_growth', rows)[0], 'fail')
        deployed = event('mcv_deployment', {'mcv': 15, 'x': 8, 'y': 8, 'deployed': 1}, house=4)
        self.assertEqual(self.check('city_or_yard_growth', rows, [deployed])[0], 'pass')
        deployed['data']['x'] = 9
        self.assertEqual(self.check('city_or_yard_growth', rows, [deployed])[0], 'fail')

    def test_distinct_rocket_case_ignores_existing_guns_and_orders(self):
        check = {'type': 'rocket_built', 'initial_rockets': 2}
        rows = [sample(house=1, rocket_turret=2, rocket_turret_built=8),
                sample('finished', house=1, rocket_turret=2, rocket_turret_built=8)]
        order = event('production_order', {'item': 12, 'accepted': 1}, house=1)
        self.assertEqual(runner.evaluate_check(check, rows, [order], {}, {})[0], 'fail')
        rows[-1]['rocket_turret_built'] = 9
        self.assertEqual(runner.evaluate_check(check, rows, [order], {}, {})[0], 'pass')
        rows[0]['rocket_turret'] = 1
        self.assertEqual(runner.evaluate_check(check, rows, [order], {}, {})[0], 'unverified')

    def test_purple_legacy_floor_needs_real_original_unpowered_city_step(self):
        check = {'type': 'legacy_unpowered_floor', 'initial_lots': 1}
        original = {'1200': dict(item=20, level=1, population=16, powered=0)}
        rows = [sample(house=7, colour_slot=7, zones=original), sample('finished', house=7, colour_slot=7)]
        setup = {'unpowered_zones_vacate': 0}
        self.assertEqual(runner.evaluate_check(check, rows, [], setup, {})[0], 'unverified')
        step = event('city_growth_sample', dict(object=1200, item=20, powered=0, old_level=1,
            new_level=1, population_before=16, population_after=16, score=-500), house=7)
        self.assertEqual(runner.evaluate_check(check, rows, [step], setup, {})[0], 'pass')
        self.assertEqual(runner.evaluate_check(check, rows, [runner.project_event(step)], setup, {})[0], 'pass')
        step['data']['score'] = -600
        self.assertEqual(runner.evaluate_check(check, rows, [runner.project_event(step)], setup, {})[0], 'pass')
        step['data'].update(new_level=0, population_after=0)
        self.assertEqual(runner.evaluate_check(check, rows, [step], setup, {})[0], 'fail')
        step['data']['object'] = 9999
        self.assertEqual(runner.evaluate_check(check, rows, [step], setup, {})[0], 'unverified')
        rows[-1]['unpowered_zones_vacate'] = 1
        self.assertEqual(runner.evaluate_check(check, rows, [], setup, {})[0], 'fail')

    def test_malformed_power_flags_cannot_clear_projected_legacy_case(self):
        case = dict(houses=[7], colour_slot=7, controllers=2,
                    checks=[dict(id='legacy', type='legacy_unpowered_floor', initial_lots=1)])
        original = {'1200': dict(item=20, level=1, population=16, powered=0)}
        rows = [sample(house=7, colour_slot=7, zones=original), sample('finished', house=7, colour_slot=7)]
        for invalid in (None, '0', [], {}, 2, -1):
            step = event('city_growth_sample', dict(object=1200, item=20, powered=invalid, old_level=1,
                new_level=1, population_before=16, population_after=16, score=-500), house=7)
            checks, feasibility = runner.evaluate_case(case, rows, [runner.project_event(step)],
                {'unpowered_zones_vacate': 0}, True, {7})
            self.assertEqual((checks[0]['status'], feasibility), ('unverified', 'unverified'))
            self.assertEqual(checks[0]['reason'], 'native_legacy_zone_power_flag_invalid')

    def test_purple_preserves_both_original_controllers(self):
        case = runner.definitions()['purple_zero_power']
        rows = [sample(house=7, colour_slot=7, bots=1), sample('finished', house=7, colour_slot=7, bots=1)]
        checks, feasibility = runner.evaluate_case(case, rows, [], {}, True, {7})
        self.assertEqual(feasibility, 'unverified')
        self.assertEqual(checks[0]['reason'], 'original_controller_count_mismatch')

    def test_research_needs_opportunity_not_fabricated_tech_requirement(self):
        self.assertEqual(self.check('research_progress')[0], 'not_applicable')
        rows = [sample(research_start_legal=1), sample('finished')]
        self.assertEqual(self.check('research_progress', rows)[0], 'fail')
        rows[-1]['research_target'] = 3
        self.assertEqual(self.check('research_progress', rows)[0], 'pass')
        rows = [sample(research_target=3, research_remaining=100),
                sample('finished', research_target=3, research_remaining=20)]
        self.assertEqual(self.check('research_progress', rows)[0], 'pass')

    def test_lab_start_and_completion_are_separate_checks(self):
        self.assertEqual(self.check('lab_built')[0], 'fail')
        rows = [sample(), sample('change', research_lab=1), sample('finished', research_lab=0, research_target=2)]
        self.assertEqual(self.check('lab_built', rows)[0], 'pass')
        self.assertEqual(self.check('level_two_research', rows)[0], 'pass')
        rows[-1]['research_target'] = 0
        self.assertEqual(self.check('level_two_research', rows)[0], 'fail')

    def test_nuclear_opportunity_and_rejection_are_not_completion(self):
        data = dict(nuclear_available=1, nuclear_site=1, cash=2000, nuclear_price=2000, nuclear=0)
        choice = event('city_generator_choice', data)
        self.assertEqual(self.check('nuclear_choice', events=[choice], setup={'concrete_required': 0})[0], 'fail')
        data['cash'] = 1999
        self.assertEqual(self.check('nuclear_choice', events=[choice], setup={'concrete_required': 0})[0], 'not_applicable')
        data.update(cash=2000, nuclear=1)
        self.assertEqual(self.check('nuclear_choice', events=[choice], setup={'concrete_required': 0})[0], 'pass')
        self.assertEqual(self.check('nuclear_choice', events=[choice], setup={'concrete_required': 1})[0], 'unverified')

    def test_lowcash_nuclear_requires_new_order_completion_and_native_payment(self):
        rows = [sample(house=4, nuclear=2, nuclear_built=0, nuclear_queued=0, economy={}),
                sample('finished', house=4, nuclear=2, nuclear_built=0, nuclear_queued=0, economy={})]
        choice = event('city_generator_choice', dict(installments=1, nuclear=1,
            nuclear_available=1, nuclear_site=1, cash=327, nuclear_price=2000), house=4)
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice])[0], 'fail')
        order = event('production_order', {'item': 25, 'accepted': 1, 'quoted_price': 2000}, 111, 4)
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice, order])[0], 'fail')
        rows[-1].update(nuclear=3, nuclear_built=1, economy={'generator_spent': 1999})
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice, order])[0], 'fail')
        rows[-1]['economy']['generator_spent'] = 2000
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice, order])[0], 'pass')
        choice['data']['nuclear_site'] = 0
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice, order])[0], 'not_applicable')
        choice['data']['nuclear_site'] = 1
        choice['data']['cash'] = 2000
        self.assertEqual(self.check('lowcash_nuclear', rows, [choice, order])[0], 'not_applicable')

    def test_cap_missing_controller_and_missing_endpoint_are_unverified(self):
        case = {'houses': [2], 'checks': [{'id': 'lf', 'type': 'light_factory_order'}]}
        samples = [sample(), sample('finished')]
        for complete, applied, telemetry in ((True, {2}, 'capped'), (False, {2}, 'complete'), (True, set(), 'complete')):
            rows, verdict = runner.evaluate_case(case, samples, [], {}, complete, applied, telemetry)
            self.assertEqual((rows[0]['status'], verdict), ('unverified', 'unverified'))
        rows, verdict = runner.evaluate_case(case, samples[:1], [], {}, True, {2})
        self.assertEqual(verdict, 'unverified')

    def test_blue_house_matches_original_loaded_roster_and_colour(self):
        case = runner.definitions()['norocketturrets']
        self.assertEqual((case['houses'], case['colour_slot']), ([6], 6))
        samples = [sample(house=6, colour_slot=6), sample('finished', house=6, colour_slot=6)]
        orders = [event('production_order', {'item': 12, 'accepted': 1}, house=6)]
        rows, verdict = runner.evaluate_case(case, samples, orders, {}, True, {6})
        self.assertEqual((rows[0]['house'], verdict), (6, 'feasible'))
        samples += [sample(house=1, colour_slot=6), sample('finished', house=1, colour_slot=6)]
        self.assertEqual(runner.evaluate_case(case, samples, orders, {}, True, {1, 6})[1], 'unverified')

    def test_event_projection_preserves_zoning_rule_inputs(self):
        for affordable in (0, 1):
            rows = []
            for cycle in range(100, 112):
                rows.append(event('zone_evaluation', {'selected': 20, 'result': 'selected',
                    'candidates': {'20': {'demand': 50, 'rank': 1}}}, cycle))
                rows.append(event('capital_plan', {'selected': 0, 'candidates': {
                    '0': {'kind': 'construction', 'reason': 'optional', 'item': 34, 'affordable': 0},
                    '1': {'item': 20, 'affordable': affordable}}}, cycle))
            projected = [runner.project_event(row) for row in rows]
            self.assertEqual(runner.check_r_admission(rows), runner.check_r_admission(projected))
            result = runner.check_r_admission(rows)['s/2']
            self.assertEqual(result['failures'],
                             ['r_never_admitted', 'optional_capital_owns_lane'] if affordable else [])
            rows.append(event('production_order', {'item': 20, 'accepted': 0}, 113))
            rows.append(event('production_order', {'item': 34, 'accepted': 1}, 110))
            self.assertEqual(runner.check_r_admission(rows)['s/2']['verdict'],
                             'pass' if affordable else 'not_applicable')

    def test_projected_capture_keeps_cap_and_requires_closed_session(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            log = run / 'profile/ai-decisions/one/events.jsonl'
            log.parent.mkdir(parents=True)
            rows = [event('session_start', {}), event('city_generator_choice', {}), event('capture_limit', {}), event('session_end', {})]
            log.write_text(''.join(json.dumps(r) + '\n' for r in rows))
            path = run / 'filtered.jsonl.gz'
            captured, status = runner.capture_events(run, path)
            self.assertEqual(status, 'capped')
            self.assertEqual(len(captured), 4)
            log.write_text(json.dumps(rows[0]) + '\n')
            self.assertEqual(runner.capture_events(run, path)[1], 'missing_or_truncated')
            log.unlink()
            self.assertEqual(runner.capture_events(run, path)[1], 'missing_or_truncated')

    def test_truncated_json_and_invalid_utf8_are_unverified_not_exceptions(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            log = run / 'profile/ai-decisions/one/events.jsonl'
            log.parent.mkdir(parents=True)
            start = (json.dumps(event('session_start', {})) + '\n').encode()
            for broken in (b'{"event":"unterminated', b'\xff\n'):
                log.write_bytes(start + broken)
                captured, status = runner.capture_events(run, run / 'projected.jsonl.gz')
                self.assertEqual(status, 'missing_or_truncated')
                self.assertEqual([e['event'] for e in captured], ['session_start'])

    def test_natural_terminal_is_observed_but_wall_censor_is_not(self):
        self.assertTrue(runner.continuation_complete({'completed': '0', 'finished': '1'}, []))
        self.assertTrue(runner.continuation_complete({'completed': '1', 'finished': '0'}, []))
        self.assertFalse(runner.continuation_complete({'completed': '0', 'finished': '0'}, []))
        self.assertFalse(runner.continuation_complete({'completed': '0', 'finished': '1'}, ['rc=1']))
        case = {'houses': [2], 'checks': [{'id': 'lf', 'type': 'light_factory_order'}]}
        rows, verdict = runner.evaluate_case(case, [sample(), sample('finished', yard=0)], [], {},
            runner.continuation_complete({'completed': '0', 'finished': '1'}, []), {2})
        self.assertEqual(verdict, 'infeasible')
        self.assertEqual(runner.evaluate_case(case, [sample(), sample('finished', yard=0)], [], {},
            True, {2}, 'missing_or_truncated')[1], 'unverified')

    def test_all_not_applicable_is_not_a_pass(self):
        self.assertEqual(runner.case_feasibility([{'status': 'not_applicable'}]), 'unverified')
        self.assertEqual(runner.case_feasibility([{'status': 'pass'}, {'status': 'not_applicable'}]), 'feasible')
        self.assertEqual(runner.case_feasibility([{'status': 'fail'}, {'status': 'pass'}]), 'infeasible')
        self.assertEqual(runner.case_feasibility([{'status': 'unverified'}, {'status': 'pass'}]), 'unverified')

    def test_compressed_csv_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.csv'
            with gzip.open(str(path) + '.gz', 'wt') as stream:
                stream.write('case,genome\nnotbuilding,default\n')
            self.assertEqual(runner.read(path), [{'case': 'notbuilding', 'genome': 'default'}])

    def test_private_revision_delta_round_trip_checks_every_original_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = next((runner.root / 'mods/dunecity').rglob('building.ini'))
            relative = source.relative_to(runner.root / 'mods/dunecity').as_posix()
            payloads = {relative: source.read_bytes(), 'QuantBot Config.ini': b'private exact config'}
            lines = ['DUNEWORKSHOP1', 'kind=mod', 'mod=']
            for relative, data in payloads.items():
                lines.append('file=%s,%d,%s' % (runner.checksum(data), len(data), relative.encode().hex()))
            manifest_bytes = ('\n'.join(lines) + '\n').encode()
            name = runner.checksum(manifest_bytes)
            original = folder / 'original/workshop/revisions' / name
            (original / 'files').mkdir(parents=True)
            (original / 'manifest').write_bytes(manifest_bytes)
            (original / 'version').write_bytes(b'exact version')
            for relative, data in payloads.items():
                target = original / 'files' / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            saved = folder / 'save.dls'
            saved.write_bytes(name.encode() + b'\0')
            revisions = runner.freeze_revisions(folder / 'original', saved)
            self.assertEqual(set(revisions[name]['files']), {'QuantBot Config.ini'})
            data = runner.canonical(revisions).encode()
            config = configparser.ConfigParser(interpolation=None)
            config['Genome Revisions'] = {'SHA256': runner.checksum(data),
                'GzipBase64': base64.b64encode(gzip.compress(data, mtime=0)).decode()}
            frozen = folder / 'input.ini'
            with frozen.open('w') as stream:
                config.write(stream)
            restored = folder / 'restored'
            self.assertEqual(runner.restore_input_revisions(frozen, restored), [name])
            for relative, original_data in payloads.items():
                self.assertEqual((restored / 'workshop/revisions' / name / 'files' / relative).read_bytes(), original_data)
            (original / 'files/QuantBot Config.ini').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                runner.freeze_revisions(folder / 'original', saved)
            config['Genome Revisions']['SHA256'] = '0' * 64
            with frozen.open('w') as stream:
                config.write(stream)
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                runner.restore_input_revisions(frozen, restored)

    def test_revision_manifest_rejects_path_escape(self):
        for relative in ('../escape', '/absolute', 'C:/absolute', 'folder\\escape'):
            line = 'file=%s,1,%s\n' % ('0' * 64, relative.encode().hex())
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                list(runner.revision_entries(line.encode()))


if __name__ == '__main__':
    unittest.main()
