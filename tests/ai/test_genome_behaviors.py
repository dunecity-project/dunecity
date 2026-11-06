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
        start_what_makes_sense=0, research_lab=0, rocket_turret_built=0)
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
        genome = {'id': 'compact', 'values': {'city_compact_power': 1}}
        encoded = base64.b64encode(json.dumps({'schema_version': 1, 'genomes': [genome]}).encode()).decode()
        args = runner.parser.parse_args(['--case', 'ainonuclear', '--manifest-b64', encoded])
        self.assertEqual(runner.schedule(args), [('ainonuclear', 'compact')])
        self.assertEqual(runner.manifest(None)['default']['values'], {})

    def test_game_rule_dependency_is_validated(self):
        genome = {'id': 'bad', 'values': {'continuous_research': 1}}
        encoded = base64.b64encode(json.dumps({'schema_version': 1, 'genomes': [genome]}).encode()).decode()
        with self.assertRaises(ValueError):
            runner.manifest(encoded)

    def test_ten_required_issues_six_adapted(self):
        cases = runner.definitions()
        self.assertEqual(len(cases), 10)
        self.assertEqual(sum(c['adapted'] for c in cases.values()), 6)
        with self.assertRaises(ValueError):
            runner.schedule(runner.parser.parse_args(['--case', 'purple_zero_power']))

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
        choice = event('city_generator_choice', dict(compact_nuclear=1, installments=1, nuclear=1,
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
        rows = []
        for cycle in range(100, 112):
            rows.append(event('zone_evaluation', {'selected': 20, 'result': 'selected',
                'candidates': {'20': {'demand': 50, 'rank': 1}}}, cycle))
            rows.append(event('capital_plan', {'selected': 0, 'candidates': {
                '0': {'kind': 'construction', 'reason': 'optional', 'item': 34, 'affordable': 0},
                '1': {'item': 20, 'affordable': 0}}}, cycle))
        for mode in (False, True):
            projected = [runner.project_event(row) for row in rows]
            self.assertEqual(runner.check_r_admission(rows, mode), runner.check_r_admission(projected, mode))
        bad = runner.check_r_admission(rows, True)['s/2']
        self.assertEqual(bad['failures'], ['r_never_admitted', 'optional_capital_owns_lane'])
        rows.append(event('drain_budget_hold', {'item': 20, 'priority': 'higher_real_drain'}, 113))
        rows.append(event('production_order', {'item': 34, 'accepted': 1}, 110))
        self.assertEqual(runner.check_r_admission(rows, True)['s/2']['verdict'], 'pass')
        self.assertEqual(runner.check_r_admission(rows, False)['s/2']['verdict'], 'not_applicable')

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
