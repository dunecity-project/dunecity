import hashlib
import importlib.util
import json
import re
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'check_web_mods', Path(__file__).resolve().parents[1] / 'check-web-mods.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class WebModPayloadTests(unittest.TestCase):
    def setUp(self):
        self.files = {name: (name + '\n').encode() for name in checker.REQUIRED}
        self.files['/mods/Tornie/checksums.sha256'] = ''.join(
            hashlib.sha256(data).hexdigest() + '  ' + name[len('/mods/Tornie/'):] + '\n'
            for name, data in self.files.items()
            if name.startswith('/mods/Tornie/') and not name.endswith('checksums.sha256')
        ).encode()
        self.skins = {'Dune2/zones/fixture/zone.ini': b'[Zone]\n',
                      'Dune2/zones/fixture/atlas.png': b'fixture image'}
        self.files.update({'/mods/dunecity/graphics_skins/' + name: data
                           for name, data in self.skins.items()})
        map_root = Path(__file__).resolve().parents[2] / 'data/maps/singleplayer'
        self.files.update({'/maps/singleplayer/' + name: (map_root / name).read_bytes()
                           for name in checker.CITY_MAPS})

    def check(self, javascript, data):
        return checker.check_payload(javascript, data, skin_payload=self.skins)

    def package(self, minified=False):
        data = b''
        records = []
        for name, content in self.files.items():
            records.append({'filename': name, 'start': len(data), 'end': len(data) + len(content)})
            data += content
        js = 'loadPackage(' + json.dumps({'files': records}) + ');'
        if minified:
            for key in ('files', 'filename', 'start', 'end'):
                js = js.replace('"' + key + '":', key + ':')
        return js, data

    def test_checks_optimized_and_unoptimized_file_tables(self):
        for minified in (False, True):
            result = self.check(*self.package(minified))
            self.assertEqual(result['verified_tornie_files'], 6)
            self.assertEqual(result['verified_dunecity_skin_files'], 2)
            self.assertEqual(result['verified_dunecity_maps'], 5)

    def test_checks_minified_scientific_notation_offsets(self):
        js, data = self.package(True)
        js = re.sub(r'(start|end): (\d+)', lambda m: m[1] + ': ' + m[2] + 'e0', js)
        self.assertEqual(self.check(js, data)['verified_tornie_files'], 6)

    def test_rejects_missing_dune2r_and_tornie_files(self):
        for name in ('/mods/Dune2R/mod.ini', '/mods/Tornie/manifest.json'):
            data = self.files.pop(name)
            with self.assertRaisesRegex(ValueError, 'Missing bundled mod file'):
                self.check(*self.package())
            self.files[name] = data

    def test_rejects_corrupt_tornie_content(self):
        self.files['/mods/Tornie/ObjectData.ini'] = b'changed'
        with self.assertRaisesRegex(ValueError, 'corrupt Tornie'):
            self.check(*self.package())

    def test_rejects_truncated_archive(self):
        js, data = self.package()
        with self.assertRaisesRegex(ValueError, 'Invalid or duplicate preload range'):
            self.check(js, data[:-1])

    def test_rejects_accidentally_embedded_optional_art(self):
        self.files['/mods/Dune2R/graphics_hd/units/tank/art.png'] = b'art'
        with self.assertRaisesRegex(ValueError, 'Optional Dune2R art'):
            self.check(*self.package())

    def test_rejects_missing_or_corrupt_dunecity_skin(self):
        name = '/mods/dunecity/graphics_skins/Dune2/zones/fixture/atlas.png'
        del self.files[name]
        with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity skin'):
            self.check(*self.package())
        self.files[name] = b'wrong image'
        with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity skin'):
            self.check(*self.package())

    def test_rejects_missing_or_corrupt_city_maps(self):
        for name in checker.CITY_MAPS:
            path = '/maps/singleplayer/' + name
            original = self.files.pop(path)
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity map'):
                self.check(*self.package())
            self.files[path] = original + b'changed'
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity map'):
                self.check(*self.package())
            self.files[path] = original


if __name__ == '__main__':
    unittest.main()
