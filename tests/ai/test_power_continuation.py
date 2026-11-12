"""Read-only power metric contracts; these are not native AI evidence."""
import ast
import csv
import gzip
import json
from pathlib import Path
import tempfile
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
            ('production_order', {'accepted': 1, 'item_name': 'Nuclear'}),
        ])
        self.assertEqual(summary[0]['generator_spent'], 2300)
        self.assertEqual(summary[0]['built_wind'], 1)
        self.assertEqual(summary[0]['built_nuclear'], 1)
        self.assertEqual(summary[0]['first_nuclear_order_elapsed_cycles'], 50)

    def test_csv_reader_matches_cluster_finalizer_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.csv'
            with path.open('w', newline='') as handle:
                writer = csv.DictWriter(handle, fieldnames=runner['FIELDS'])
                writer.writeheader()
                writer.writerow({'arm': 'on', 'sha256': 'exact', 'wall_s': '1', 'error': ''})
            self.assertEqual(runner['read'](path)[0]['arm'], 'on')
            self.assertEqual(runner['read'](path.with_name('missing.csv')), [])


if __name__ == '__main__':
    unittest.main()
