"""Offline registration, timing, source-integrity and bounded-atlas checks."""
import configparser
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('infantry_runtime_pack', ROOT / 'scripts/package-dune2r-infantry.py')
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        source = Image.new('RGBA', (1024, 1024), (21, 31, 41, 0))
        source.paste((220, 10, 20, 255), (650, 710, 670, 730))
        source.paste((10, 20, 30, 128), (800, 900, 804, 904))
        self.source = self.root / 'source.png'
        source.save(self.source)
        frame = {'file': str(self.source), 'sha256': PACK.sha256(self.source), 'duration_ms': 60}
        sequences = []
        for state in PACK.STATES:
            for heading in PACK.HEADINGS:
                frames = [dict(frame)]
                if state == 'Movement':
                    frames.append(dict(frame, duration_ms=91))
                sequences.append({'state': state, 'direction': heading,
                                  'loop': state in ('Idle', 'Movement', 'DamageAftermath'),
                                  'frames': frames})
        self.selection = {'schema': 'dune2r-unit-runtime-source-v1', 'source_canvas': [1024, 1024],
                          'frame_size': [512, 512], 'item_id': 32, 'house_id': 0,
                          'base_size': [40, 40], 'scale': 1.0, 'anchor': [256, 451],
                          'sequences': sequences, 'source_bindings': {str(self.source): frame['sha256']}}
        self.json_path = self.root / 'local-selection.json'

    def read(self, selection=None):
        self.json_path.write_text(json.dumps(selection or self.selection), encoding='utf-8')
        return PACK.read_selection(self.json_path)

    def test_registered_pixels_and_alpha_are_not_fitted_or_multiplied(self):
        sequence = self.selection['sequences'][8]  # Movement.east
        atlas = PACK.make_atlas(sequence)
        with Image.open(self.source) as original:
            expected = original.resize((512, 512), Image.Resampling.NEAREST)
        self.assertEqual(atlas.crop((0, 0, 512, 512)).tobytes(), expected.tobytes())
        self.assertEqual(atlas.crop((512, 0, 1024, 512)).tobytes(), expected.tobytes())
        self.assertNotEqual(expected.getchannel('A').getbbox()[0], 0)
        self.assertEqual(expected.getpixel((325, 355)), (220, 10, 20, 255))
        self.assertEqual(expected.getpixel((400, 450)), (10, 20, 30, 128))

    def test_exact_durations_and_common_anchor_manifest(self):
        parser = configparser.ConfigParser()
        parser.read_string(PACK.manifest_text(self.read()))
        self.assertEqual(parser['Movement.east']['durationsms'], '60,91')
        self.assertEqual(parser['CombatReturn.east']['loop'], 'false')
        self.assertTrue(all(parser[s]['anchorx'] == '256' and parser[s]['anchory'] == '451'
                            for s in parser if '.' in s))
        self.assertEqual(parser['Unit']['itemid'], '32')
        self.assertEqual(parser['Unit']['houseid'], '0')

    def test_atlas_limit_accepts_64_and_rejects_65(self):
        self.assertEqual(PACK.layout(64), (8, 8))
        self.assertEqual(PACK.layout(39), (8, 5))
        for count in (0, 65):
            with self.assertRaises(ValueError):
                PACK.layout(count)

    def test_source_mutation_is_rejected_before_output(self):
        self.source.write_bytes(b'not the accepted source')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            self.read()
        self.assertFalse((self.root / 'harkonneninfantry').exists())

    def test_invalid_duration_direction_loop_and_anchor_are_rejected(self):
        mutations = [lambda s: s['sequences'][0]['frames'][0].update(duration_ms=0),
                     lambda s: s['sequences'][0]['frames'][0].update(duration_ms=60001),
                     lambda s: s['sequences'][0].update(direction='north_east'),
                     lambda s: s['sequences'][0].update(loop=False),
                     lambda s: s.update(anchor=[256, 512]),
                     lambda s: s.update(item_id=46)]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                candidate = copy.deepcopy(self.selection)
                mutate(candidate)
                with self.assertRaises(ValueError):
                    self.read(candidate)

    def test_crop_or_opaque_source_is_rejected(self):
        for size, color in (((900, 900), (0, 0, 0, 0)), ((1024, 1024), (50, 50, 50, 255))):
            with self.subTest(size=size, color=color):
                Image.new('RGBA', size, color).save(self.source)
                candidate = copy.deepcopy(self.selection)
                digest = PACK.sha256(self.source)
                for sequence in candidate['sequences']:
                    for frame in sequence['frames']:
                        frame['sha256'] = digest
                candidate['source_bindings'][str(self.source)] = digest
                with self.assertRaises(ValueError):
                    self.read(candidate)

    def test_new_pack_only_never_overwrites_and_has_no_private_manifest(self):
        selection = self.read()
        destination = self.root / 'harkonneninfantry'
        report = PACK.write_pack(selection, destination)
        self.assertEqual(report['files'], 49)
        self.assertEqual(report['frames'], 56)
        self.assertEqual(report, PACK.verify_pack(selection, destination))
        self.assertFalse((destination / 'unit.json').exists())
        with self.assertRaisesRegex(ValueError, 'NEW'):
            PACK.write_pack(selection, destination)
        (destination / 'unexpected.txt').write_text('reject me')
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            PACK.verify_pack(selection, destination)


class ShippedPackTests(unittest.TestCase):
    def test_shipped_pack_directions_timing_geometry_and_runtime_only_files(self):
        root = ROOT / 'mods/Dune2R/graphics_hd/units/harkonneninfantry'
        manifest = configparser.ConfigParser()
        manifest.read(root / 'unit.ini', encoding='utf-8')
        self.assertEqual(manifest['Unit']['itemid'], '32')
        self.assertEqual(manifest['Unit']['houseid'], '0')
        self.assertEqual(manifest['Render']['basewidth'], '40')
        sections = {f'{state}.{heading}' for state in PACK.STATES for heading in PACK.HEADINGS}
        self.assertEqual(set(manifest.sections()) - {'Unit', 'Render'}, sections)
        files = {'unit.ini'}
        poses = 0
        for section in sorted(sections):
            data = manifest[section]
            count, columns, rows = (int(data[k]) for k in ('frames', 'columns', 'rows'))
            durations = list(map(int, data['durationsms'].split(',')))
            self.assertEqual(len(durations), count)
            self.assertTrue(all(1 <= n <= 60000 for n in durations))
            self.assertEqual((int(data['anchorx']), int(data['anchory'])), (256, 451))
            self.assertLessEqual(count, columns * rows)
            path = root / data['atlas']
            self.assertTrue(path.resolve().is_relative_to(root.resolve()))
            files.add(data['atlas'])
            with Image.open(path) as atlas:
                self.assertEqual(atlas.mode, 'RGBA')
                self.assertEqual(atlas.size, (columns * 512, rows * 512))
                self.assertLessEqual(max(atlas.size), PACK.MAX_AXIS)
                self.assertLessEqual(atlas.width * atlas.height, PACK.MAX_PIXELS)
                for index in range(count):
                    frame = atlas.crop(((index % columns) * 512, (index // columns) * 512,
                                        (index % columns + 1) * 512, (index // columns + 1) * 512))
                    self.assertEqual(frame.getextrema()[3][0], 0)
                    self.assertIsNotNone(frame.getchannel('A').getbbox())
            poses += count
        self.assertEqual(poses, 386)
        self.assertEqual(files, {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()})


if __name__ == '__main__':
    unittest.main()
