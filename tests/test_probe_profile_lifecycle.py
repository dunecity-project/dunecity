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


if __name__=='__main__':unittest.main()
