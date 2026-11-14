"""Campaign research failures must stay visible and cannot qualify a treatment."""
import copy
import csv
import json
from pathlib import Path
import tempfile
import unittest
import campaign_research as subject


def manifest_fixture(root):
    styles = root / 'tests/ai/play-styles'
    styles.mkdir(parents=True, exist_ok=True)
    for name in ('control', 'candidate'):
        (styles / (name + '.ini')).write_text('[Force Concentration]\n')
    case = dict(house='harkonnen', mission=21, seed=9901, helper='brutal', enemy='hard', mode='full', mod='vanilla', minutes=80)
    for stage, seed in (('pilot', 9901), ('confirmation', 10001)):
        (root / (stage + '.json')).write_text(json.dumps(dict(schema='campaign-research-cases/1', cases=[dict(case, seed=seed)])))
    plans = {
        'campaign-all-maps': [dict(id=f'{h}-{n}', house=h, mission=n) for h in subject.HOUSES for n in range(1,23)],
        'scenario-controls': [dict(id=k, category=k) for k in ('controls','skirmish','team','ladder','aaa')],
        'historical': [dict(id='historical-deck')]}
    for name, rows in plans.items(): (root / (name + '.json')).write_text(json.dumps(rows))
    return dict(schema='dunecity-experiment/1', population='campaign', id='campaign-support',
        mechanism='Earlier useful attacks', falsifier='No effective dispatch change', scope='Player helper only',
        off_contract='Current policy', stopping_rule='Fixed budget', snapshot=dict(revision='a'*40,cwd=str(root)),
        baseline=dict(release='869',source_commit='a'*40,config_identity='native',population='campaign',independent=True),
        observed_failure=['loss'],successful_counterexamples=['win'],negative_learning=[], configs=dict(off='control',on='candidate'),
        primary=dict(metric='won',direction='higher',minimum_effect=.1),
        design=dict(cluster_fields=['seed'],stopping='fixed-budget',alpha=.05,power=.8,finalists=1),
        budget=dict(max_games=4,max_wall_s=1000), promotion_controls=list(plans),
        control_plans={k:k+'.json' for k in plans},
        stages={s:dict(case_manifest=s+'.json',args=[],**(dict(locked_at='2026-11-15T00:00:00Z',untouched=True) if s=='confirmation' else {})) for s in ('pilot','confirmation')})


def csv_file(path, rows):
    with path.open('w') as f:
        writer=csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)


class CampaignResearchTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.m=manifest_fixture(self.root);self.path=self.root/'experiment.json';self.path.write_text(json.dumps(self.m))
        self.case=subject.cases(self.root/'pilot.json')[0];self.gid=subject.game_id(self.case)

    def game(self, arm='off', samples=True, events=True):
        p=self.root/arm;p.mkdir(exist_ok=True)
        setup={k:'same' for k in subject.SETUP};setup['config_hash']='native'
        row=dict(id=self.gid,category='campaign',seed='9901',mod='vanilla',map='harkonnen level 8 mission 21',
            sides='harkonnen:1:QB enemies:2:QH',limit='80',campaign_mode='full',partner='brutal',enemy_ai='quantbot',enemy_difficulty='hard',
            source_commit='a'*40,source_fingerprint='fingerprint',digest='digest',outcome='lost',minutes='80',error='',
            helper_style=self.m['configs'][arm],ai_configs=json.dumps({'0':arm,'1':'enemy'}),setup_identity=json.dumps(setup))
        csv_file(p/'games.csv',[row])
        if samples:
            csv_file(p/'samples.csv',[dict(id=self.gid,house='0',minute='80',credits='30',army_value='0',harvesters='0',repair_yards='1',spice_gross='100',loss_value='500',kill_value='100')])
        if events:
            csv_file(p/'campaign-ai.csv',[dict(id=self.gid,line='minute=0 house=0 role=partner difficulty=3 event=campaign_opening {}')])
        return p

    def test_frozen_budget_holdout_and_full_control_coverage(self):
        self.assertEqual(subject.validate_manifest(self.m,self.root)['games_per_case'],2)
        p=self.root/'confirmation.json';d=json.loads(p.read_text());d['cases'][0]['seed']=9901;p.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'reuses'):subject.validate_manifest(self.m,self.root)

    def test_missing_map_is_not_complete_control_plan(self):
        p=self.root/'campaign-all-maps.json';d=json.loads(p.read_text());p.write_text(json.dumps(d[:-1]))
        with self.assertRaisesRegex(ValueError,'66'):subject.validate_manifest(self.m,self.root)

    def test_missing_telemetry_keeps_game_with_unknown_measurement(self):
        data=subject.game_evidence(self.game(samples=False,events=False))
        self.assertEqual(len(data),1)
        self.assertIsNone(data[self.gid]['metrics']['credits']['final'])
        self.assertIn('missing-player-samples',data[self.gid]['issues'])
        self.assertIn('missing-campaign-events',data[self.gid]['issues'])

    def test_changed_enemy_is_not_helper_effect(self):
        a=subject.game_evidence(self.game());b=subject.game_evidence(self.game('on'))
        b[self.gid]['configs']['1']='changed'
        self.assertIn('enemy-policy-unverified-or-changed',subject.pair_records(a,b)[0]['issues'])

    def test_full_identity_does_not_merge_single_and_full(self):
        a=subject.game_evidence(self.game());b=subject.game_evidence(self.game('on'))
        row=b.pop(self.gid);b[self.gid.replace('full-campaign','single-mission')]=row
        pairs=subject.pair_records(a,b)
        self.assertEqual(len(pairs),2)
        self.assertTrue(all('unmatched-game' in r['issues'] for r in pairs))

    def test_missing_both_arms_is_reported_and_cannot_promote(self):
        a=self.root/'off';b=self.root/'on';a.mkdir();b.mkdir()
        r=subject.experiment_report(self.path,'pilot',a,b)
        self.assertEqual(r['games'][0]['issues'],['missing-both-arms'])
        self.assertFalse(r['facts']['promotion_ready'])
        self.assertFalse(r['facts']['complete'])

    def test_each_game_requires_evidence_bound_review(self):
        a=self.game();b=self.game('on');r=subject.experiment_report(self.path,'pilot',a,b)
        self.assertEqual(r['pending_reviews'],[self.gid])
        self.assertTrue(r['facts']['complete'])
        self.assertFalse(r['facts']['promotion_ready'])
        key=r['games'][0]['review_key'];self.assertNotEqual(key,subject.digest(dict(r['games'][0],outcome_change='won->lost')))

    def test_world_or_source_drift_is_invalid(self):
        a=subject.game_evidence(self.game());b=subject.game_evidence(self.game('on'))
        b[self.gid]['setup']['map_hash']='different';b[self.gid]['source']='b'*40
        issues=subject.pair_records(a,b)[0]['issues']
        self.assertIn('missing-or-changed-world-identity',issues);self.assertIn('source-mismatch',issues)

    def test_duplicate_case_rejected(self):
        p=self.root/'pilot.json';d=json.loads(p.read_text());d['cases']*=2;p.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'Duplicate'):subject.cases(p)


if __name__=='__main__':unittest.main()
