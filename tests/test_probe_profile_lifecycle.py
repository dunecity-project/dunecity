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


if __name__=='__main__':unittest.main()
