import gzip
import hashlib
from contextlib import nullcontext
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

module_path=Path(__file__).parent/'performance/probe_profile.py'
spec=importlib.util.spec_from_file_location('probe_profile_under_test',module_path)
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)


class ProbeLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve();self.repo=self.root/'repo';self.repo.mkdir()

    def test_prefers_correct_staging_cache_and_rejects_copied_cache(self):
        old=self.repo/'build';old.mkdir();(old/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL=/wrong/checkout\n')
        with self.assertRaises(RuntimeError):probe.default_build_dir(self.repo)
        staging=self.repo/'build-int';staging.mkdir();(staging/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL='+str(self.repo)+'\n')
        self.assertEqual(probe.default_build_dir(self.repo),staging)

    def test_unmanaged_host_keeps_standalone_behavior(self):
        profile=self.repo/'profile'
        with patch.dict(os.environ,{'HOME':str(self.root)}):
            with probe.managed_profile(profile):
                profile.mkdir();(profile/'keep').write_text('keep')
        self.assertTrue((profile/'keep').exists())

    def test_installed_lifecycle_preserves_original_and_logs(self):
        source=Path.home()/'.local/share/agent-workspace/workspace.py'
        if not source.is_file():self.skipTest('managed-host helper is not installed')
        home=self.root/'home';helper=home/'.local/share/agent-workspace/workspace.py';helper.parent.mkdir(parents=True)
        shutil.copy2(source,helper)
        config=home/'.config/agent-workspace/projects.json';config.parent.mkdir(parents=True)
        build=self.repo/'build';build.mkdir()
        config.write_text(json.dumps({'projects':{'test':{'checkout':str(self.repo),'build':str(build),'artifact_roots':[str(build)]}}}))
        original=self.root/'original';(original/'mods/vanilla').mkdir(parents=True)
        (original/'mods/vanilla/asset').write_text('original')
        (original/'Dune City.ini').write_text('[General]\nCustom Game Mod=vanilla\n')
        profile=build/'probe/profile'
        with patch.dict(os.environ,{'HOME':str(home),'AGENT_WORKSPACE_CONFIG':str(config),'AGENT_WORKSPACE_STATE':str(home/'state')}):
            with probe.managed_profile(profile):
                probe.prepare_profile(profile,original);(profile/'run.log').write_text('evidence')
            self.assertFalse((profile/'mods').exists());self.assertTrue((profile/'run.log').exists())
            self.assertEqual((original/'mods/vanilla/asset').read_text(),'original')
            with self.assertRaises(RuntimeError):
                with probe.managed_profile(profile):
                    probe.prepare_profile(profile,original);raise RuntimeError('probe failure')
            self.assertTrue((profile/'mods/vanilla/asset').exists())

    def test_discard_keeps_evidence_and_telemetry_unless_asked(self):
        profile=self.repo/'game/profile'
        for name in ('mods/dunecity','workshop/revisions/abc','ai-decisions/1','replay'):
            (profile/name).mkdir(parents=True)
        (profile/'ai-decisions/1/events.jsonl').write_text('{}\n');(profile/'Dune City.log').write_text('log')
        probe.discard_profile_assets(profile)
        self.assertFalse((profile/'mods').exists() or (profile/'workshop/revisions').exists())
        self.assertTrue((profile/'ai-decisions/1/events.jsonl').exists() and (profile/'replay').exists())
        probe.discard_profile_assets(profile,telemetry=True)
        self.assertFalse((profile/'ai-decisions').exists());self.assertTrue((profile/'Dune City.log').exists())

    def test_removes_readonly_revision_before_reusing_probe_output(self):
        out=self.repo/'probe'
        immutable=out/'profile/workshop/revisions/abc/files/GameOptions.ini'
        immutable.parent.mkdir(parents=True);immutable.write_text('snapshot')
        immutable.chmod(0o444)
        probe.remove_tree(out)
        self.assertFalse(out.exists())
        out.mkdir(exist_ok=False)
        probe.remove_tree(out)
        probe.remove_tree(out)

    def test_remove_tree_does_not_hide_real_errors(self):
        with patch.object(probe.shutil,'rmtree',side_effect=PermissionError('still locked')):
            with self.assertRaisesRegex(PermissionError,'still locked'):
                probe.remove_tree(self.repo/'probe')

    def test_prune_runs_keeps_the_newest(self):
        for i in range(5):
            run=self.repo/('run-%d'%i);run.mkdir();os.utime(run,(1000+i,1000+i))
        (self.repo/'other').mkdir()
        probe.prune_runs(self.repo,'run-',2)
        self.assertEqual(sorted(p.name for p in self.repo.iterdir()),['other','run-3','run-4'])

    def test_clone_tree_copies_contents(self):
        source=self.root/'src/a';source.mkdir(parents=True);(source/'f').write_text('x')
        probe.clone_tree(self.root/'src',self.repo/'dst')
        self.assertEqual((self.repo/'dst/a/f').read_text(),'x')

    def test_unmanaged_scratch_is_removed_on_success_and_kept_on_failure(self):
        with patch.dict(os.environ,{'HOME':str(self.root)}):
            with probe.scratch_dir('unit') as path:
                (path/'game').mkdir()
            self.assertFalse(path.exists())
            with self.assertRaises(RuntimeError):
                with probe.scratch_dir('unit') as failed:
                    raise RuntimeError('batch failed')
            self.assertTrue(failed.exists());shutil.rmtree(failed)
            explicit=self.repo/'mine'
            with probe.scratch_dir('unit',explicit) as path:
                self.assertEqual(path,explicit)
            self.assertTrue(explicit.exists())
            with self.assertRaises(SystemExit):probe.require_free_space(10**9)
            self.assertGreater(probe.require_free_space(0)['free_gb'],0)

    def test_managed_scratch_is_leased_under_the_scratch_root(self):
        installed=Path.home()/'.local/share/agent-workspace'
        if not (installed/'scratch.py').is_file():self.skipTest('managed-host scratch helper is not installed')
        home=self.root/'home';helper=home/'.local/share/agent-workspace';helper.mkdir(parents=True)
        for name in ('workspace.py','scratch.py'):shutil.copy2(installed/name,helper/name)
        config=home/'.config/agent-workspace/projects.json';config.parent.mkdir(parents=True)
        scratch_root=self.root/'work'
        config.write_text(json.dumps({'projects':{'dunecity':{'checkout':str(self.repo),'build':str(self.repo/'build'),
                                                              'artifact_roots':[],'scratch_root':str(scratch_root)}}}))
        env={'HOME':str(home),'AGENT_WORKSPACE_CONFIG':str(config),'AGENT_WORKSPACE_STATE':str(home/'state'),
             'AGENT_WORKSPACE_OWNER':'omp:unit:test'}
        with patch.dict(os.environ,env):
            with probe.scratch_dir('duel') as path:
                self.assertEqual(path.parent,scratch_root/'omp_unit_test')
                self.assertEqual(json.loads((path/'.agent-lease.json').read_text())['pid'],os.getpid())
            self.assertFalse(path.exists())
            with self.assertRaises(RuntimeError):
                with probe.scratch_dir('duel') as failed:
                    raise RuntimeError('batch failed')
            self.assertEqual(json.loads((failed/'.agent-lease.json').read_text())['state'],'failed')
            with probe.scratch_dir('duel') as kept:
                game=kept/'game';(game/'profile/mods/x').mkdir(parents=True)
                probe.finish_game(kept,game,True)
            self.assertEqual(json.loads((kept/'.agent-lease.json').read_text())['state'],'failed')
            self.assertEqual(probe.require_free_space(0)['min_free_gb'],0)
            with probe.headless_mod_cache() as cache:
                lease = json.loads((cache.parent/'.agent-lease.json').read_text())
                self.assertEqual(lease['state'], 'active')
                self.assertEqual(lease['pid'], os.getpid())
                self.assertTrue(lease['reader_cache'])
                info = cache.parent.stat()
                self.assertEqual(json.loads((cache.parent/'.cache-reader.lock').read_text()),
                    {'format': 1, 'root': [info.st_dev, info.st_ino], 'uid': os.getuid()})
                sealed = cache/'publication/mods'
                sealed.mkdir(parents=True)
                (sealed/'asset').write_bytes(b'leased immutable bytes')
                (sealed/'asset').chmod(0o444)
                sealed.chmod(0o555)
                self.assertEqual(sealed.stat().st_mode & 0o222, 0)
            self.assertFalse(cache.parent.exists())
            with self.assertRaisesRegex(RuntimeError, 'failed cache lane'):
                with probe.headless_mod_cache() as cache:
                    sealed = cache/'publication/mods'
                    sealed.mkdir(parents=True)
                    (sealed/'asset').write_bytes(b'failed immutable bytes')
                    (sealed/'asset').chmod(0o444)
                    sealed.chmod(0o555)
                    raise RuntimeError('failed cache lane')
            self.assertEqual(json.loads((cache.parent/'.agent-lease.json').read_text())['state'], 'failed')
            self.assertTrue(sealed.stat().st_mode & 0o200)
            self.assertEqual((sealed/'asset').stat().st_mode & 0o222, 0)
            probe.remove_tree(cache.parent)

    def test_finish_game_deletes_successes_and_keeps_capped_small_failures(self):
        scratch=self.repo/'scratch'
        def game(name):
            out=scratch/name
            for sub in ('profile/mods/dunecity','profile/workshop/revisions/a','profile/ai-decisions/1','profile/save'):
                (out/sub).mkdir(parents=True)
            (out/'run.log').write_text('log');return out
        probe.finish_game(scratch,game('ok'),False)
        self.assertFalse((scratch/'ok').exists())
        for i in range(probe.FAILED_GAMES_KEEP+2):
            probe.finish_game(scratch,game('bad%d'%i),True)
        kept=sorted(p.name for p in (scratch/'failed').iterdir())
        self.assertEqual(len(kept),probe.FAILED_GAMES_KEEP)
        self.assertEqual(sorted(p.name for p in scratch.iterdir()),['failed'])
        first=scratch/'failed/bad0'
        self.assertTrue((first/'run.log').exists() and (first/'profile/save').exists())
        self.assertFalse((first/'profile/mods').exists() or (first/'profile/workshop/revisions').exists()
                         or (first/'profile/ai-decisions').exists())

    def test_unmanaged_batch_with_failed_games_is_kept(self):
        with patch.dict(os.environ,{'HOME':str(self.root)}):
            with probe.scratch_dir('unit') as path:
                (path/'g').mkdir();probe.finish_game(path,path/'g',True)
            self.assertTrue((path/'failed/g').is_dir());shutil.rmtree(path)

    def test_managed_cache_does_not_silently_fall_back_to_unregistered_scratch(self):
        with patch.dict(os.environ, {'HOME': str(self.root/'missing-helper')}):
            with self.assertRaisesRegex(RuntimeError, 'registered agent-workspace scratch'):
                with probe.headless_mod_cache():
                    self.fail('An unregistered cache was admitted')

    def test_copies_only_the_revisions_the_mods_and_saves_pin(self):
        source=self.root/'player';store=source/'workshop/revisions'
        mod,mapped,dep,other=('a'*64),('b'*64),('c'*64),('d'*64)
        for name,extra in ((mod,''),(mapped,'mod='+dep+'\n'),(dep,''),(other,'')):
            (store/name/'files').mkdir(parents=True);(store/name/'manifest').write_text('DUNEWORKSHOP1\nkind=x\n'+extra)
        (source/'workshop/owner').write_text('secret');(source/('mods/ws-'+mod)).mkdir(parents=True)
        save=self.root/'game.dls';save.write_bytes(b'\x00DuneCity\x40\x00\x00\x00'+mapped.encode()+b'\x00')
        profile=self.root/'profile'
        self.assertEqual(probe.copy_revisions(source,profile,[save]),sorted([mod,mapped,dep]))
        self.assertEqual(sorted(p.name for p in (profile/'workshop/revisions').iterdir()),sorted([mod,mapped,dep]))
        self.assertFalse((profile/'workshop/owner').exists())
        self.assertEqual(probe.copy_revisions(source,self.root/'empty'),[mod])

    def test_restores_only_pinned_fixture_rules_and_retired_art(self):
        config=b'historical rules\n';art=b'historical image bytes'
        config_hash=hashlib.sha256(config).hexdigest();art_hash=hashlib.sha256(art).hexdigest()
        manifest=('DUNEWORKSHOP1\nkind=mod\n'
            +f'file={config_hash},{len(config)},{b"ObjectData.ini".hex()}\n'
            +f'file={art_hash},{len(art)},{b"graphics_skins/old.png".hex()}\n').encode()
        revision=hashlib.sha256(manifest).hexdigest()
        archive=self.repo/'tests/saves/data/workshop/revisions'/revision
        (archive/'files').mkdir(parents=True)
        (archive/'manifest').write_bytes(manifest);(archive/'version').write_text('3')
        (archive/'files/ObjectData.ini').write_bytes(config)
        current=self.repo/'mods/dunecity/graphics_skins/old.png';current.parent.mkdir(parents=True)
        current.write_bytes(b'changed current artwork')
        old=self.repo/'tests/performance/data/eight-house-384/files'/(art_hash+'.png.gz')
        old.parent.mkdir(parents=True);old.write_bytes(gzip.compress(art))
        save=self.root/'game.dls';save.write_text(revision+'\n'+'f'*64)
        profile=self.root/'profile'
        with patch.object(probe,'__file__',str(self.repo/'tests/performance/probe_profile.py')):
            self.assertEqual(probe.restore_fixture_revisions(profile,[save]),[revision])
        restored=profile/'workshop/revisions'/revision
        self.assertEqual((restored/'manifest').read_bytes(),manifest)
        self.assertEqual((restored/'version').read_text(),'3')
        self.assertEqual((restored/'files/ObjectData.ini').read_bytes(),config)
        self.assertEqual((restored/'files/graphics_skins/old.png').read_bytes(),art)
        self.assertEqual([p.name for p in (profile/'workshop/revisions').iterdir()],[revision])

    def test_changed_archived_fixture_bytes_are_rejected(self):
        payload=b'original rules'
        manifest=('DUNEWORKSHOP1\nkind=mod\nfile='+hashlib.sha256(payload).hexdigest()
            +f',{len(payload)},{b"ObjectData.ini".hex()}\n').encode()
        revision=hashlib.sha256(manifest).hexdigest()
        archive=self.repo/'tests/saves/data/workshop/revisions'/revision
        (archive/'files').mkdir(parents=True)
        (archive/'manifest').write_bytes(manifest);(archive/'version').write_text('1')
        (archive/'files/ObjectData.ini').write_bytes(b'changed rules')
        save=self.root/'game.dls';save.write_text(revision)
        profile=self.root/'profile'
        with patch.object(probe,'__file__',str(self.repo/'tests/performance/probe_profile.py')):
            with self.assertRaisesRegex(RuntimeError,'Archived fixture bytes changed'):
                probe.restore_fixture_revisions(profile,[save])
        self.assertFalse((profile/'workshop/revisions'/revision/'manifest').exists())


class SharedHeadlessModTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.build = self.root / 'build'
        self.runtime = self.build / ('bin/dunecity.app/Contents/Resources' if probe.sys.platform == 'darwin' else 'bin')
        self.artwork = self.runtime / 'mods/dunecity/graphics_skins'
        self.artwork.mkdir(parents=True)
        (self.artwork / 'building.ini').write_bytes(b'[Building]\nObjPic=Repair\n')
        (self.artwork / 'atlas.png').write_bytes(b'pixels-a')
        config = self.runtime / 'config'
        config.mkdir()
        for name in ('ObjectData.ini', 'QuantBot Config.ini'):
            (config / (name + '.default')).write_text(name + ' installer rules\n')
        self.binary = self.root / 'native-probe'
        self.binary.write_bytes(b'current native producer')
        self.fingerprint = hashlib.sha256(b'current source').hexdigest()
        self.declarations = [self.artwork]
        self.shadow = False
        self.metadata = b'native metadata'
        self.scratch_count = 0

    def scratch(self, name):
        self.assertEqual(name, 'headless-mod-seed')
        self.scratch_count += 1
        directory = self.root / ('seed-' + str(self.scratch_count))
        directory.mkdir()
        return nullcontext(directory)

    def native_seed(self, command, *, cwd, env, stdout, stderr, check, timeout):
        self.assertEqual(command[0], str(self.binary))
        self.assertEqual(env['DUNECITY_HEADLESS_ASSETS'], '1')
        self.assertEqual(env['DUNECITY_HEADLESS_SEED_ONLY'], '1')
        self.assertNotIn('DUNECITY_SHARED_MODS', env)
        self.assertTrue(check)
        self.assertEqual(timeout, 120)
        for name in ('vanilla', 'dunecity'):
            mod = Path(env['DUNECITY_USERDIR']) / 'mods' / name
            mod.mkdir(parents=True)
            for template in ('ObjectData.ini', 'QuantBot Config.ini'):
                (mod / template).write_bytes((self.runtime / 'config' / (template + '.default')).read_bytes())
            (mod / 'GameOptions.ini').write_text(name + ' native options\n')
            (mod / 'mod.ini').write_bytes(self.metadata)
        if self.shadow:
            (mod / 'graphics_skins').mkdir()
        for path in self.declarations:
            stdout.write('HEADLESS_MOD_ARTWORK_V1: ' + str(path) + '\n')
        return probe.subprocess.CompletedProcess(command, 0)

    def prepare(self, fingerprint=None, copier=None):
        if copier is None:
            copier = lambda source, target: shutil.copytree(source, target, dirs_exist_ok=True)
        with patch.object(probe, 'scratch_dir', side_effect=self.scratch), \
             patch.object(probe.subprocess, 'run', side_effect=self.native_seed) as native, \
             patch.object(probe, 'clone_tree', side_effect=copier) as copies:
            result = probe.shared_headless_mods(self.binary, self.build,
                {'BALANCE_MOD': 'vanilla', 'BALANCE_REPAIR_CAPACITY_PROBE': '1', 'DUNECITY_SHARED_MODS': '/not-input'},
                fingerprint or self.fingerprint, lambda lock: None)
        return result, native, copies

    def previous_publication(self, cache):
        """A real on-disk format-2 publication from a completed earlier consumer."""
        art_files = {p.relative_to(self.artwork).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in self.artwork.rglob('*') if p.is_file()}
        templates = {name: hashlib.sha256((self.runtime / 'config' / (name + '.default')).read_bytes()).hexdigest()
                     for name in ('ObjectData.ini', 'QuantBot Config.ini')}
        bundled = hashlib.sha256(json.dumps({'artwork': art_files, 'templates': templates},
                                           sort_keys=True).encode()).hexdigest()
        identity = {'format': 2, 'binary': hashlib.sha256(self.binary.read_bytes()).hexdigest(),
                    'source': self.fingerprint, 'bundled': bundled}
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
        publication = cache / key
        mods = publication / 'mods'
        for name in ('vanilla', 'dunecity'):
            mod = mods / name
            mod.mkdir(parents=True)
            for template in templates:
                (mod / template).write_bytes((self.runtime / 'config' / (template + '.default')).read_bytes())
            (mod / 'GameOptions.ini').write_text(name + ' native options\n')
            (mod / 'mod.ini').write_bytes(self.metadata)
        shutil.copytree(self.artwork, mods / 'dunecity/graphics_skins')
        files = {p.relative_to(mods).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in mods.rglob('*') if p.is_file()}
        (publication / 'manifest.json').write_text(json.dumps({'identity': identity, 'files': files}, sort_keys=True))
        for path in publication.rglob('*'):
            path.chmod(0o555 if path.is_dir() else 0o444)
        return mods, identity

    @unittest.skipUnless(os.name == 'posix', 'persistent Mac/Linux checkout permissions')
    def test_lane_cache_pin_survives_checkout_and_retires_before_next_preparation(self):
        import fcntl
        import subprocess
        import sys

        source_path = Path(__file__).parent / 'ai/source_provenance.py'
        source_spec = importlib.util.spec_from_file_location('cache_source_provenance', source_path)
        source = importlib.util.module_from_spec(source_spec)
        source_spec.loader.exec_module(source)
        checkout = self.root / 'checkout'

        def prepare_checkout():
            checkout.mkdir()
            subprocess.run(['git', 'init', '--quiet', str(checkout)], check=True)
            (checkout / 'rules.ini').write_bytes(b'identical source rules\n')
            generated = checkout / 'build/intermediate'
            generated.mkdir(parents=True)
            (generated / 'output').write_bytes(b'disposable build output')

        def fingerprint():
            return source.source_fingerprint(checkout, checkout / 'build', checkout / 'results')

        def reuse(cache):
            with probe.headless_mod_reader(cache) as pin:
                return probe.shared_headless_mods(self.binary, self.build, {}, self.fingerprint,
                    lambda handle: fcntl.flock(handle, fcntl.LOCK_EX), cache_root=cache,
                    pass_fds=(pin.fileno(),))

        prepare_checkout()
        self.fingerprint = fingerprint()
        with patch.dict(os.environ, {'HOME': str(self.root / 'standalone-home')}):
            with probe.headless_mod_cache(standalone=True) as first:
                self.assertFalse(first.is_relative_to(checkout))
                mods, identity = self.previous_publication(first)
                self.assertEqual(reuse(first), mods)
                payload = mods / 'dunecity/graphics_skins/atlas.png'
                digest = hashlib.sha256(payload.read_bytes()).hexdigest()
                # An actual live consumer pins the external sealed bytes while
                # the complete disposable checkout is removed and prepared again.
                reader = subprocess.Popen([sys.executable, '-c',
                    'import hashlib,pathlib,sys; p=pathlib.Path(sys.argv[1]); '
                    'before=p.read_bytes(); print(hashlib.sha256(before).hexdigest(),flush=True); '
                    'sys.stdin.read(); assert p.read_bytes()==before; '
                    'assert p.stat().st_mode & 0o222 == 0', str(payload)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
                try:
                    self.assertEqual(reader.stdout.readline().strip(), digest)
                    self.assertIsNone(reader.poll())
                    self.assertEqual(fingerprint(), self.fingerprint)
                    shutil.rmtree(checkout)
                    prepare_checkout()
                    self.assertEqual(fingerprint(), self.fingerprint)
                    self.assertEqual(hashlib.sha256(payload.read_bytes()).hexdigest(), digest)
                    self.assertEqual(mods.stat().st_mode & 0o222, 0)
                    self.assertEqual(payload.stat().st_mode & 0o222, 0)
                    self.assertEqual(reuse(first), mods)
                finally:
                    reader.communicate(input='release', timeout=10)
                self.assertEqual(reader.returncode, 0)
                self.assertEqual(mods.stat().st_mode & 0o222, 0)
                key = mods.parent.name
            self.assertFalse(first.parent.exists())
            shutil.rmtree(checkout)
            prepare_checkout()
            with probe.headless_mod_cache(standalone=True) as second:
                next_mods, next_identity = self.previous_publication(second)
                self.assertNotEqual(first, second)
                self.assertEqual(next_mods.parent.name, key)
                self.assertEqual(next_identity, identity)
                self.assertEqual(reuse(second), next_mods)
                self.assertEqual(fingerprint(), self.fingerprint)
            self.assertFalse(second.parent.exists())

    @unittest.skipUnless(os.name == 'posix', 'persistent Mac/Linux checkout permissions')
    def test_failed_lane_keeps_sweepable_cache_without_unsealing_files(self):
        with patch.dict(os.environ, {'HOME': str(self.root / 'standalone-home')}):
            with self.assertRaisesRegex(RuntimeError, 'lane failed'):
                with probe.headless_mod_cache(standalone=True) as cache:
                    mods, _ = self.previous_publication(cache)
                    raise RuntimeError('lane failed')
            try:
                self.assertTrue(cache.parent.exists())
                self.assertTrue(all(path.stat().st_mode & 0o200 for path in cache.rglob('*') if path.is_dir()))
                self.assertTrue(all(path.stat().st_mode & 0o222 == 0 for path in cache.rglob('*') if path.is_file()))
                self.assertEqual((mods / 'dunecity/mod.ini').read_bytes(), self.metadata)
            finally:
                probe.remove_tree(cache.parent)

    @unittest.skipUnless(os.name == 'posix', 'persistent Mac/Linux process-group lifetime')
    def test_command_descendant_releases_sealed_bytes_before_lane_cache_retirement(self):
        import subprocess
        import sys

        receipt = self.root/'consumer-receipt.json'
        consumer = '''
import hashlib, json, pathlib, signal, sys
payload, receipt = map(pathlib.Path, sys.argv[1:])
def finish(signum, frame):
    receipt.write_text(json.dumps({'sha256': hashlib.sha256(payload.read_bytes()).hexdigest(),
        'file_mode': payload.stat().st_mode, 'directory_mode': payload.parent.stat().st_mode}))
    raise SystemExit(0)
signal.signal(signal.SIGTERM, finish)
signal.alarm(10)
receipt.with_suffix('.ready').write_text(str(payload))
while True:
    signal.pause()
'''
        leader = '''
import os, pathlib, subprocess, sys, time
sys.path.insert(0, sys.argv[3])
from probe_profile import headless_mod_reader
cache = pathlib.Path(os.environ['DUNECITY_HEADLESS_MOD_CACHE'])
pin = headless_mod_reader(cache)
payload = cache/'publication/mods/asset'
payload.parent.mkdir(parents=True)
payload.write_bytes(b'owned live consumer bytes')
payload.chmod(0o444)
payload.parent.chmod(0o555)
reader = subprocess.Popen([sys.executable, '-c', sys.argv[1], str(payload), sys.argv[2]],
                          pass_fds=(pin.fileno(),))
pin.close()
ready = pathlib.Path(sys.argv[2]).with_suffix('.ready')
deadline = time.monotonic() + 5
while not ready.exists():
    if reader.poll() is not None or time.monotonic() >= deadline:
        raise RuntimeError('consumer did not become ready')
    time.sleep(.01)
# Leave the live reader in the owned group. The existing runner must drain it
# before the cache context is allowed to change any directory permissions.
'''
        environment = dict(os.environ, HOME=str(self.root/'standalone-home'),
                           RUNNER_ENVIRONMENT='github-hosted')
        subprocess.run([sys.executable, str(module_path), '--', sys.executable, '-c', leader,
                        consumer, str(receipt), str(module_path.parent)], env=environment, check=True, timeout=20)
        result = json.loads(receipt.read_text())
        self.assertEqual(result['sha256'], hashlib.sha256(b'owned live consumer bytes').hexdigest())
        self.assertEqual(result['file_mode'] & 0o222, 0)
        self.assertEqual(result['directory_mode'] & 0o222, 0)
        payload = Path(receipt.with_suffix('.ready').read_text())
        self.assertFalse(payload.parents[3].exists())

    @unittest.skipUnless(os.name == 'posix', 'POSIX inherited reader pins and detached sessions')
    def test_detached_reader_survives_launcher_and_blocks_retirement_until_released(self):
        import subprocess
        import sys

        ready, release, receipt = (self.root/name for name in ('detached-ready', 'release-reader', 'reader-done'))
        consumer = '''
import hashlib, pathlib, signal, sys, time
payload, ready, release, receipt = map(pathlib.Path, sys.argv[1:])
signal.signal(signal.SIGTERM, signal.SIG_IGN)
before = payload.read_bytes()
ready.write_bytes(before)
deadline = time.monotonic() + 10
while not release.exists():
    assert payload.read_bytes() == before
    assert payload.stat().st_mode & 0o222 == 0
    assert payload.parent.stat().st_mode & 0o222 == 0
    if time.monotonic() >= deadline:
        raise RuntimeError('test reader release deadline')
    time.sleep(.01)
receipt.write_text(hashlib.sha256(payload.read_bytes()).hexdigest())
'''
        launcher = '''
import pathlib, subprocess, sys, time
pin = int(sys.argv[1])
reader = subprocess.Popen([sys.executable, '-c', *sys.argv[2:]], start_new_session=True, pass_fds=(pin,))
ready = pathlib.Path(sys.argv[4])
deadline = time.monotonic() + 5
while not ready.exists():
    if reader.poll() is not None or time.monotonic() >= deadline:
        raise RuntimeError('detached reader failed to start')
    time.sleep(.01)
# Exit without waiting: the real reader keeps the inherited open description.
'''
        with patch.dict(os.environ, {'HOME': str(self.root/'standalone-home')}):
            with self.assertRaisesRegex(RuntimeError, 'readers still active'):
                with probe.headless_mod_cache(standalone=True, retirement_timeout=0) as cache:
                    pin = probe.headless_mod_reader(cache)
                    try:
                        mods, _ = self.previous_publication(cache)
                        payload = mods/'dunecity/graphics_skins/atlas.png'
                        child = subprocess.Popen([sys.executable, '-c', launcher, str(pin.fileno()), consumer,
                            str(payload), str(ready), str(release), str(receipt)], pass_fds=(pin.fileno(),))
                    finally:
                        pin.close()
                    self.assertEqual(child.wait(timeout=10), 0)
                    self.assertEqual(ready.read_bytes(), payload.read_bytes())
            try:
                self.assertEqual(payload.stat().st_mode & 0o222, 0)
                self.assertEqual(payload.parent.stat().st_mode & 0o222, 0)
                self.assertTrue((cache.parent/'.cache-retiring').exists())
                with self.assertRaisesRegex(RuntimeError, 'retiring'):
                    probe.headless_mod_reader(cache)
            finally:
                release.write_text('released')
                with probe._retiring_cache(cache.parent, probe._cache_root_identity(cache.parent), 5):
                    self.assertEqual(receipt.read_text(), hashlib.sha256(payload.read_bytes()).hexdigest())
                    for path in cache.rglob('*'):
                        if path.is_dir():
                            path.chmod(path.stat().st_mode | 0o200)
                    probe.remove_tree(cache)
                probe.remove_tree(cache.parent)

    @unittest.skipUnless(os.name == 'posix', 'POSIX cancellation during reader barrier acquisition')
    def test_interrupted_retirement_keeps_live_reader_bytes_sealed(self):
        import signal
        import subprocess
        import sys

        def interrupt(signum, frame):
            raise KeyboardInterrupt('cancel retirement')

        signaler = '''
import os, pathlib, signal, sys, time
marker, parent = pathlib.Path(sys.argv[1]), int(sys.argv[2])
deadline = time.monotonic() + 5
while not marker.exists():
    if os.getppid() != parent or time.monotonic() >= deadline:
        raise RuntimeError('retirement did not begin')
    time.sleep(.01)
if os.getppid() == parent:
    os.kill(parent, signal.SIGUSR1)
'''
        previous = signal.signal(signal.SIGUSR1, interrupt)
        pin = None
        try:
            with patch.dict(os.environ, {'HOME': str(self.root/'standalone-home')}):
                with self.assertRaises(KeyboardInterrupt):
                    with probe.headless_mod_cache(standalone=True, retirement_timeout=5) as cache:
                        pin = probe.headless_mod_reader(cache)
                        mods, _ = self.previous_publication(cache)
                        child = subprocess.Popen([sys.executable, '-c', signaler,
                            str(cache.parent/'.cache-retiring'), str(os.getpid())])
                self.assertEqual(child.wait(timeout=5), 0)
                self.assertEqual(mods.stat().st_mode & 0o222, 0)
                self.assertEqual((mods/'dunecity/mod.ini').stat().st_mode & 0o222, 0)
        finally:
            signal.signal(signal.SIGUSR1, previous)
            if pin is not None:
                pin.close()
                with probe._retiring_cache(cache.parent, probe._cache_root_identity(cache.parent), 5):
                    for path in cache.rglob('*'):
                        if path.is_dir():
                            path.chmod(path.stat().st_mode | 0o200)
                    probe.remove_tree(cache)
                probe.remove_tree(cache.parent)

    @unittest.skipUnless(os.name == 'posix', 'POSIX SIGKILL exit and inherited reader barrier')
    def test_uncooperative_group_reader_is_unpinned_before_successful_retirement(self):
        import subprocess
        import sys
        sys.path.insert(0, str(module_path.parents[1]/'menu'))
        from probe_process import ProbeProcesses

        ready = self.root/'uncooperative-ready'
        consumer = '''
import pathlib, signal, sys, time
payload, ready = map(pathlib.Path, sys.argv[1:])
signal.signal(signal.SIGTERM, signal.SIG_IGN)
assert payload.stat().st_mode & 0o222 == 0
ready.write_bytes(payload.read_bytes())
time.sleep(60)
'''
        launcher = '''
import pathlib, subprocess, sys, time
pin = int(sys.argv[1])
reader = subprocess.Popen([sys.executable, '-c', *sys.argv[2:]], pass_fds=(pin,))
ready = pathlib.Path(sys.argv[4])
deadline = time.monotonic() + 5
while not ready.exists():
    if reader.poll() is not None or time.monotonic() >= deadline:
        raise RuntimeError('group reader failed to start')
    time.sleep(.01)
'''
        with patch.dict(os.environ, {'HOME': str(self.root/'standalone-home')}):
            with probe.headless_mod_cache(standalone=True, retirement_timeout=5) as cache:
                with probe.headless_mod_reader(cache) as pin:
                    mods, _ = self.previous_publication(cache)
                    payload = mods/'dunecity/mod.ini'
                    owner = ProbeProcesses(grace=.01)
                    result = owner.run([sys.executable, '-c', launcher, str(pin.fileno()), consumer,
                        str(payload), str(ready)], pass_fds=(pin.fileno(),), check=True, timeout=10)
                    self.assertEqual(result.returncode, 0)
                    self.assertEqual(ready.read_bytes(), self.metadata)
                    self.assertEqual(payload.parent.stat().st_mode & 0o222, 0)
            self.assertFalse(cache.parent.exists())

    @unittest.skipUnless(os.name == 'posix', 'persistent Mac/Linux no-symlink retirement')
    def test_retirement_rejects_link_before_thawing_any_directory(self):
        outside = self.root/'outside'
        outside.mkdir()
        (outside/'keep').write_bytes(b'not cache data')
        with patch.dict(os.environ, {'HOME': str(self.root/'standalone-home')}):
            with self.assertRaisesRegex(RuntimeError, 'Unsafe retired headless cache'):
                with probe.headless_mod_cache(standalone=True) as cache:
                    mods, _ = self.previous_publication(cache)
                    (cache/'linked').symlink_to(outside, target_is_directory=True)
            try:
                self.assertEqual(mods.stat().st_mode & 0o222, 0)
                self.assertEqual((outside/'keep').read_bytes(), b'not cache data')
            finally:
                (cache/'linked').unlink()
                for path in cache.rglob('*'):
                    if path.is_dir():
                        path.chmod(path.stat().st_mode | 0o200)
                probe.remove_tree(cache.parent)

    def assert_unpublished(self):
        cache = self.build / 'headless-mod-cache'
        self.assertFalse(list(cache.glob('*/manifest.json')))
        self.assertFalse(list(cache.glob('*.incoming-*')))

    def test_cold_publication_has_complete_native_rules_and_exact_art_warm_reuses_it(self):
        mods, native, copies = self.prepare()
        self.assertIsNotNone(mods)
        native.assert_called_once()
        self.assertEqual(copies.call_count, 3)
        for name in ('vanilla', 'dunecity'):
            self.assertEqual((mods / name / 'mod.ini').read_bytes(), self.metadata)
            self.assertEqual((mods / name / 'ObjectData.ini').read_bytes(),
                             (self.runtime / 'config/ObjectData.ini.default').read_bytes())
        art = mods / 'dunecity/graphics_skins'
        self.assertEqual({p.relative_to(art): p.read_bytes() for p in art.rglob('*') if p.is_file()},
                         {p.relative_to(self.artwork): p.read_bytes() for p in self.artwork.rglob('*') if p.is_file()})
        manifest = json.loads((mods.parent / 'manifest.json').read_text())
        self.assertEqual(len(manifest['files']), 10)
        self.assertEqual(manifest['identity']['format'], 2)
        self.assertEqual(manifest['identity']['source'], self.fingerprint)
        if os.name != 'nt':
            self.assertEqual((art / 'atlas.png').stat().st_mode & 0o222, 0)
        again, native, copies = self.prepare()
        self.assertEqual(again, mods)
        native.assert_not_called()
        copies.assert_not_called()

    def test_new_source_reuses_verified_art_but_current_engine_authors_metadata(self):
        old, _, _ = self.prepare()
        # Older complete native publications include this noncanonical cache stamp.
        art = old / 'dunecity/graphics_skins'
        art.chmod(0o755)
        stamp = art / '.bundled-skin-fingerprint'
        stamp.write_text('0123456789abcdef\n')
        manifest_path = old.parent / 'manifest.json'
        manifest_path.chmod(0o644)
        manifest = json.loads(manifest_path.read_text())
        manifest['files']['dunecity/graphics_skins/.bundled-skin-fingerprint'] = hashlib.sha256(stamp.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest))
        stamp.chmod(0o444)
        art.chmod(0o555)
        manifest_path.chmod(0o444)
        self.binary.write_bytes(b'next native producer')
        self.metadata = b'new native metadata'
        current, native, copies = self.prepare(hashlib.sha256(b'next source').hexdigest())
        native.assert_called_once()
        self.assertNotEqual(current, old)
        self.assertEqual(copies.call_args_list[-1].args[0], art)
        self.assertFalse((current / 'dunecity/graphics_skins/.bundled-skin-fingerprint').exists())
        self.assertEqual((current / 'dunecity/mod.ini').read_bytes(), self.metadata)
        self.assertEqual((old / 'dunecity/mod.ini').read_bytes(), b'native metadata')

    def test_changed_installer_art_gets_a_new_seal_without_reusing_stale_bytes(self):
        old, _, _ = self.prepare()
        (self.artwork / 'atlas.png').write_bytes(b'pixels-b')
        current, native, copies = self.prepare()
        native.assert_called_once()
        self.assertNotEqual(current, old)
        self.assertEqual(copies.call_args_list[-1].args[0], self.artwork.resolve())
        self.assertEqual((current / 'dunecity/graphics_skins/atlas.png').read_bytes(), b'pixels-b')
        self.assertEqual((old / 'dunecity/graphics_skins/atlas.png').read_bytes(), b'pixels-a')

    def test_malformed_unrelated_file_tables_skip_to_valid_next_artwork_candidate(self):
        old, _, _ = self.prepare()
        cache = old.parent.parent
        valid = cache / ('f' * 64)
        old.parent.rename(valid)
        malformed = [('0' * 64, None), ('1' * 64, [])]
        for name, files in malformed:
            directory = cache / name
            directory.mkdir()
            (directory / 'manifest.json').write_text(json.dumps({'files': files}))
        current, native, copies = self.prepare(hashlib.sha256(b'next source with bad older caches').hexdigest())
        self.assertIsNotNone(current)
        native.assert_called_once()
        self.assertEqual(copies.call_args_list[-1].args[0], valid / 'mods/dunecity/graphics_skins')
        self.assertEqual((current / 'dunecity/graphics_skins/atlas.png').read_bytes(), b'pixels-a')
        self.assertEqual(len(json.loads((current.parent / 'manifest.json').read_text())['files']), 10)
        for name, files in malformed:
            self.assertEqual(json.loads((cache / name / 'manifest.json').read_text()), {'files': files})

    def test_missing_ambiguous_and_escaping_art_declarations_never_publish(self):
        outside = self.root / 'outside-bundle'
        outside.mkdir()
        (outside / 'keep').write_bytes(b'not an installer input')
        for declarations in ([], [self.artwork, self.artwork], [outside]):
            with self.subTest(declarations=declarations):
                self.declarations = declarations
                result, _, copies = self.prepare()
                self.assertIsNone(result)
                copies.assert_not_called()
                self.assert_unpublished()
        self.assertEqual((outside / 'keep').read_bytes(), b'not an installer input')

    def test_native_target_shadow_and_failed_art_copy_never_publish(self):
        self.shadow = True
        result, _, _ = self.prepare()
        self.assertIsNone(result)
        self.assert_unpublished()
        self.shadow = False
        def failed_copy(source, target):
            if source == self.artwork.resolve():
                raise OSError('interrupted artwork copy')
            shutil.copytree(source, target, dirs_exist_ok=True)
        result, _, _ = self.prepare(copier=failed_copy)
        self.assertIsNone(result)
        self.assert_unpublished()


if __name__=='__main__':unittest.main()
