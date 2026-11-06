"""Read-only metric/behavior contracts; these are not native AI evidence."""
import ast
import contextlib
import copy
import csv
import gzip
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

HARNESS = Path(__file__).with_name('power-continuation.py')
tree = ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS))
prefix = []
for node in tree.body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    prefix.append(node)
else:
    raise AssertionError('missing queue AST boundary')
runner = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=prefix, type_ignores=[]), str(HARNESS), 'exec'), runner)


def snapshot(phase, wind, nuclear, spent):
    return {'marker': 'POWER_SAVE_HOUSE', 'data': {
        'phase': phase, 'house': 0, 'cycle': 100 if phase == 'loaded' else 1000,
        'credits': 3000, 'produced': 100 if phase == 'loaded' else 2100, 'required': 90,
        'generators': {'Windtrap': {'built': wind}, 'Nuclear': {'built': nuclear}},
        'economy': {'generator_spent': spent},
    }}


class PowerMetrics(unittest.TestCase):
    def measure(self, events):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'events.jsonl.gz'
            with gzip.open(path, 'wt') as handle:
                for kind, data in events:
                    handle.write(json.dumps({'house': 0, 'cycle': 150, 'event': kind, 'data': data}) + '\n')
            return runner['power_summary']([snapshot('loaded', 2, 1, 500), snapshot('finished', 3, 2, 2800)], path)

    def test_real_payment_delta_and_completed_counts(self):
        summary = self.measure([
            ('city_generator_choice', {'compact_nuclear': 1, 'nuclear': 1, 'nuclear_available': 1,
                                       'nuclear_site': 1, 'cash': 2200, 'nuclear_price': 2000}),
            ('production_order', {'accepted': 1, 'item_name': 'Nuclear'}),
        ])
        self.assertEqual(summary[0]['generator_spent'], 2300)
        self.assertEqual(summary[0]['built_wind'], 1)
        self.assertEqual(summary[0]['built_nuclear'], 1)
        self.assertEqual(summary[0]['first_nuclear_order_elapsed_cycles'], 50)
        self.assertTrue(runner['compact_behavior_passes'](summary))

    def test_funded_rejection_is_not_an_unaffordable_or_illegal_reactor(self):
        common = {'compact_nuclear': 0, 'nuclear': 0, 'nuclear_available': 1,
                  'nuclear_site': 1, 'cash': 2200, 'nuclear_price': 2000}
        events = [('city_generator_choice', common)]
        for change in ({'cash': 1999}, {'nuclear_site': 0}, {'nuclear_available': 0}):
            value = dict(common, **change)
            events.append(('city_generator_choice', value))
        summary = self.measure(events)
        self.assertEqual(summary[0]['funded_nuclear_rejections'], 1)
        self.assertFalse(runner['compact_behavior_passes'](summary))

    def test_rc_zero_and_a_selected_candidate_are_not_paid_completion(self):
        summary = self.measure([('city_generator_choice', {
            'compact_nuclear': 1, 'nuclear': 1, 'nuclear_available': 1,
            'nuclear_site': 1, 'cash': 2200, 'nuclear_price': 2000})])
        self.assertFalse(runner['compact_behavior_passes'](summary))
        ordered = copy.deepcopy(summary)
        ordered[0]['nuclear_orders'] = 1
        ordered[0]['built_nuclear'] = 0
        self.assertFalse(runner['compact_behavior_passes'](ordered))
        ordered[0]['built_nuclear'] = 1
        self.assertTrue(runner['compact_behavior_passes'](ordered))
        unpaid = copy.deepcopy(ordered)
        unpaid[0]['generator_spent'] = 0
        self.assertFalse(runner['compact_behavior_passes'](unpaid))
        ordered[0]['funded_nuclear_rejections'] = 1
        self.assertFalse(runner['compact_behavior_passes'](ordered))

    def test_csv_reader_matches_cluster_finalizer_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.csv'
            with path.open('w', newline='') as handle:
                writer = csv.DictWriter(handle, fieldnames=runner['FIELDS'])
                writer.writeheader()
                writer.writerow({'arm': 'on', 'sha256': 'exact', 'wall_s': '1', 'error': ''})
            self.assertEqual(runner['read'](path)[0]['arm'], 'on')
            self.assertEqual(runner['read'](path.with_name('missing.csv')), [])

    def test_eligible_house_does_not_hide_another_houses_funded_rejection(self):
        good = {'compact_choices': 1, 'nuclear_orders': 1, 'built_nuclear': 1,
                'generator_spent': 2000, 'funded_nuclear_rejections': 0}
        bad = dict(good, compact_choices=0, nuclear_orders=0, funded_nuclear_rejections=1)
        self.assertFalse(runner['compact_behavior_passes']([good, bad]))

    def test_read_only_report_rechecks_behavior_instead_of_trusting_csv_success(self):
        good = {'compact_choices': 1, 'nuclear_orders': 1, 'built_nuclear': 1,
                'generator_spent': 2000, 'funded_nuclear_rejections': 0}
        with tempfile.TemporaryDirectory() as directory:
            result = Path(directory)
            with (result / 'games.csv').open('w', newline='') as handle:
                writer = csv.DictWriter(handle, fieldnames=runner['FIELDS'])
                writer.writeheader()
                writer.writerow({'arm': 'on', 'sha256': 'exact', 'wall_s': '1', 'error': ''})
            args = SimpleNamespace(results=directory, arms=['on'], verify_compact=True)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(runner['report'](args))  # Missing native behavior evidence.
                (result / 'on-power-summary.json').write_text(json.dumps([good]))
                self.assertFalse(runner['report'](args))
                (result / 'on-power-summary.json').write_text(json.dumps([dict(good, built_nuclear=0)]))
                self.assertTrue(runner['report'](args))
                args.arms = ['off']
                self.assertTrue(runner['report'](args))  # The requested native arm never ran.


if __name__ == '__main__':
    unittest.main()
