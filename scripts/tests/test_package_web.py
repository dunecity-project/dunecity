import importlib.util
from pathlib import Path
import tempfile
import unittest
from html.parser import HTMLParser
import json
import hashlib

spec = importlib.util.spec_from_file_location('package_web', Path(__file__).resolve().parents[1] / 'package-web.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_relay_sources_are_exact_and_plaintext_is_explicit_loopback_only(self):
        self.assertEqual(packager.relay_sources('https://relay.example:443'),
                         ['https://relay.example:443'])
        self.assertEqual(packager.relay_sources('http://127.0.0.1:8787', True),
                         ['http://127.0.0.1:8787'])
        for origin in ('http://relay.example', 'https://relay.example/path',
                       'https://user:pass@relay.example', 'https://relay.example?x=1',
                       'https://relay.example#x', "https://relay.example; script-src *",
                       'https://relay.example:99999', 'null', 'https://*'):
            with self.subTest(origin=origin), self.assertRaises(ValueError):
                packager.relay_sources(origin, True)
        with self.assertRaises(ValueError):
            packager.relay_sources('http://127.0.0.1:8787')

    def test_relay_csp_preserves_other_directives(self):
        policy = "default-src 'none'; connect-src 'self'; script-src 'self'"
        sources = packager.relay_sources('https://relay.example')
        actual = packager.allow_relay_connections(policy, sources)
        self.assertEqual(actual, "default-src 'none'; connect-src 'self' https://relay.example; script-src 'self'")
        with self.assertRaises(RuntimeError):
            packager.allow_relay_connections("default-src 'none'", sources)

    def test_versions_minified_and_quoted_html_and_hashes_final_files(self):
        for quote in ('', '"', "'"):
            with self.subTest(quote=quote), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                output = root / 'build/bin'
                output.mkdir(parents=True)
                (output / 'dunecity.html').write_text(f'<link href={quote}shell.css{quote} rel=stylesheet><script src={quote}shell.js{quote}></script><script src={quote}p2p-direct.js{quote}></script><script src={quote}dunecity.js{quote} async></script>')
                for name in ('dunecity.js','dunecity.wasm','dunecity.data','p2p-direct.js'):
                    (output / name).write_bytes(b'test artifact')
                packager.package(root/'build', root/'play')
                html = (root/'play/index.html').read_text()
                self.assertEqual(html.count('?v='),4)
                manifest = json.loads((root/'play/build.json').read_text())
                for name, digest in manifest['sha256'].items():
                    self.assertEqual(hashlib.sha256((root/'play'/name).read_bytes()).hexdigest(),digest)
                self.assertIn('p2p-direct.js', manifest['artifacts'])
                self.assertEqual(manifest['gameTransport'], 'direct-webrtc')
                self.assertEqual(manifest['relayOrigins'], [])
                (output / 'p2p-direct.js').write_bytes(b'changed transport, identical wasm')
                packager.package(root/'build', root/'play')
                self.assertNotEqual(html, (root/'play/index.html').read_text())
                (output / 'p2p-direct.js').unlink()
                with self.assertRaisesRegex(RuntimeError, 'Missing browser artifact'):
                    packager.package(root/'build', root/'play')

    def test_oversized_data_is_split_into_ordered_parts_and_rejoins_to_the_manifest_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            output = root / 'build/bin'
            output.mkdir(parents=True)
            (output / 'dunecity.html').write_text('<link href=shell.css><script src=shell.js></script><script src=p2p-direct.js></script><script src=dunecity.js></script>')
            for name in ('dunecity.js', 'dunecity.wasm', 'p2p-direct.js'):
                (output / name).write_bytes(b'small')
            data = bytes(range(256)) * 41  # 10496 bytes
            (output / 'dunecity.data').write_bytes(data)
            original = packager.PART_BYTES
            packager.PART_BYTES = 4096
            try:
                packager.package(root/'build', root/'play')
                manifest = json.loads((root/'play/build.json').read_text())
                parts = manifest['split']['dunecity.data']
                self.assertEqual(parts, ['dunecity.data.part-000', 'dunecity.data.part-001', 'dunecity.data.part-002'])
                self.assertFalse((root/'play/dunecity.data').exists())
                joined = b''.join((root/'play'/part).read_bytes() for part in parts)
                self.assertEqual(joined, data)
                self.assertEqual(hashlib.sha256(joined).hexdigest(), manifest['sha256']['dunecity.data'])
                # A smaller repackage drops the parts again instead of leaving stale ones.
                (output / 'dunecity.data').write_bytes(b'small data')
                packager.package(root/'build', root/'play')
                self.assertEqual(list((root/'play').glob('dunecity.data.part-*')), [])
                self.assertNotIn('dunecity.data', json.loads((root/'play/build.json').read_text()).get('split', {}))
            finally:
                packager.PART_BYTES = original


if __name__ == '__main__': unittest.main()
