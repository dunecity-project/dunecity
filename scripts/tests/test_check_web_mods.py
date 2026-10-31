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

FAMILY_MODS = ('Tornie', 'TornieLite', 'Jericho', 'JerichoLite', 'ArcaKev', 'ArcaKevLite')


class WebModPayloadTests(unittest.TestCase):
    def setUp(self):
        self.files = {name: (name + '\n').encode() for name in checker.REQUIRED}
        for mod in FAMILY_MODS:
            prefix = f'/mods/{mod}/'
            self.files[prefix + 'campaign/scena001.ini'] = b'[BASIC]\nTechLevel=1\n'
            self.files[prefix + 'checksums.sha256'] = ''.join(
                hashlib.sha256(data).hexdigest() + '  ' + name[len(prefix):] + '\n'
                for name, data in self.files.items()
                if name.startswith(prefix) and not name.endswith('checksums.sha256')
            ).encode()
        self.frames = {name: ('frame ' + name).encode() for name in checker.FACTION_FRAMES}
        self.files.update({'/frames/' + name: value for name, value in self.frames.items()})
        self.skins = {'Dune2/zones/fixture/zone.ini': b'[Zone]\n',
                      'Dune2/zones/fixture/atlas.png': b'fixture image'}
        self.files.update({'/mods/dunecity/graphics_skins/' + name: data
                           for name, data in self.skins.items()})
        map_root = Path(__file__).resolve().parents[2] / 'data/maps/singleplayer'
        self.files.update({'/maps/singleplayer/' + name: (map_root / name).read_bytes()
                           for name in (*checker.CITY_MAPS, *checker.CITY_MAP_METADATA)})

    def check(self, javascript, data):
        return checker.check_payload(javascript, data, skin_payload=self.skins, frame_payload=self.frames)

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
            self.assertEqual(result['verified_family_files'], {mod: 7 for mod in FAMILY_MODS})
            self.assertEqual(result['verified_dunecity_skin_files'], 2)
            self.assertEqual(result['verified_shared_faction_frames'], 12)
            self.assertEqual(result['shared_faction_frame_bytes'], sum(map(len, self.frames.values())))
            self.assertEqual(result['verified_dunecity_maps'], len(checker.CITY_MAPS))
            self.assertEqual(result['verified_dunecity_map_metadata'], len(checker.CITY_MAP_METADATA))

    def test_checks_minified_scientific_notation_offsets(self):
        js, data = self.package(True)
        js = re.sub(r'(start|end): (\d+)', lambda m: m[1] + ': ' + m[2] + 'e0', js)
        self.assertEqual(self.check(js, data)['verified_family_files'],
                         {mod: 7 for mod in FAMILY_MODS})

    def test_rejects_missing_bundled_mod_files(self):
        for name in checker.REQUIRED:
            with self.subTest(name=name):
                data = self.files.pop(name)
                with self.assertRaisesRegex(ValueError, 'Missing bundled mod file: ' + re.escape(name)):
                    self.check(*self.package())
                self.files[name] = data

    def test_rejects_missing_family_payload(self):
        for mod in FAMILY_MODS:
            with self.subTest(mod=mod):
                name = f'/mods/{mod}/campaign/scena001.ini'
                data = self.files.pop(name)
                with self.assertRaisesRegex(ValueError, f'Missing or corrupt {mod} payload:'):
                    self.check(*self.package())
                self.files[name] = data

    def test_rejects_corrupt_family_payload(self):
        for mod in FAMILY_MODS:
            for relative in ('ObjectData.ini', 'campaign/scena001.ini'):
                with self.subTest(mod=mod, relative=relative):
                    name = f'/mods/{mod}/' + relative
                    data = self.files[name]
                    self.files[name] = data + b'changed'
                    with self.assertRaisesRegex(ValueError, f'Missing or corrupt {mod} payload:'):
                        self.check(*self.package())
                    self.files[name] = data

    def test_rejects_invalid_family_checksum_manifests(self):
        for mod in FAMILY_MODS:
            name = f'/mods/{mod}/checksums.sha256'
            original = self.files[name]
            first = original.splitlines(keepends=True)[0]
            cases = (
                (b'\n', f'Empty {mod} checksum manifest'),
                (b'not a checksum\n', f'Invalid {mod} checksum entry'),
                (original + first, f'Invalid {mod} checksum path'),
                (b'0' * 64 + b'  ../Tornie/ObjectData.ini\n', f'Invalid {mod} checksum path'),
            )
            for manifest, error in cases:
                with self.subTest(mod=mod, manifest=manifest):
                    self.files[name] = manifest
                    with self.assertRaisesRegex(ValueError, error):
                        self.check(*self.package())
            self.files[name] = original

    def test_rejects_missing_or_corrupt_shared_faction_frames(self):
        for name in self.frames:
            path = '/frames/' + name
            original = self.files.pop(path)
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt shared faction frame'):
                self.check(*self.package())
            self.files[path] = original + b'changed'
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt shared faction frame'):
                self.check(*self.package())
            self.files[path] = original

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

    def test_rejects_missing_or_corrupt_city_map_metadata(self):
        for name in checker.CITY_MAP_METADATA:
            path = '/maps/singleplayer/' + name
            original = self.files.pop(path)
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity map metadata'):
                self.check(*self.package())
            self.files[path] = original + b'changed'
            with self.assertRaisesRegex(ValueError, 'Missing or corrupt DuneCity map metadata'):
                self.check(*self.package())
            self.files[path] = original


if __name__ == '__main__':
    unittest.main()
