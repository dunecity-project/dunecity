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
