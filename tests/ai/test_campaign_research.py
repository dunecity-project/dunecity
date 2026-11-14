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
    (root / 'options.ini').write_text('[Game Options]\nFog of War = false\n')
    plans = {
        'campaign-all-maps': [dict(id=f'campaign/test/vanilla/{h}-m{n}/1', house=h, mission=n, map=f'{h} level {subject.level(n)} mission {n}') for h in subject.HOUSES for n in range(1,23)],
        'scenario-controls': [dict(id=k+'/test', category=k) for k in ('controls','skirmish','team','ladder','aaa')],
        'historical': [dict(id='historical-deck')]}
    for name, rows in plans.items(): (root / (name + '.json')).write_text(json.dumps(rows))
    return dict(schema='dunecity-experiment/1', population='campaign', id='campaign-support',
        mechanism='Earlier useful attacks', falsifier='No effective dispatch change', scope='Player helper only',
        off_contract='Current policy', stopping_rule='Fixed budget', snapshot=dict(revision='a'*40,cwd=str(root)),
        baseline=dict(release='869',source_commit='a'*40,config_identity='native',population='campaign',independent=True),
        observed_failure=['loss'],successful_counterexamples=['win'],negative_learning=[], configs=dict(off='control',on='candidate'),
        game_options='options.ini', primary=dict(metric='won',direction='higher',minimum_effect=.1),
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
            csv_file(p/'samples.csv',[dict(id=self.gid,house='0',minute='80',cycle='300000',credits='30',army_value='0',harvesters='0',repair_yards='1',spice_gross='100',loss_value='500',kill_value='100')])
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

    def test_final_sample_in_same_minute_is_not_duplicate(self):
        p=self.game(); rows=subject.table(p,'samples.csv')
        rows.append(dict(rows[0],cycle='300050',credits='0'))
        csv_file(p/'samples.csv',rows)
        self.assertNotIn('invalid-or-duplicate-samples',subject.game_evidence(p)[self.gid]['issues'])
        rows.append(rows[0]);csv_file(p/'samples.csv',rows)
        self.assertIn('invalid-or-duplicate-samples',subject.game_evidence(p)[self.gid]['issues'])

    def test_options_bytes_are_part_of_frozen_provenance(self):
        first=subject.validate_manifest(self.m,self.root)
        (self.root/'options.ini').write_text('[Game Options]\nFog of War = true\n')
        second=subject.validate_manifest(self.m,self.root)
        self.assertNotEqual(first['game_options']['sha256'],second['game_options']['sha256'])

    def control_schedule(self):
        path=self.root/'small-control.json'
        path.write_text(json.dumps([dict(id=self.gid, seed=9901, mod='vanilla', map='harkonnen level 8 mission 21', minutes=80, sides='harkonnen:1:QB enemies:2:QH')]))
        return path

    def test_controls_cannot_compare_candidate_against_itself(self):
        candidate=self.game('on')
        report=subject.control_report(candidate,candidate,self.control_schedule(),'campaign-all-maps','candidate',control_config='control')
        self.assertFalse(report['complete'])

    def test_observer_controls_resolve_first_seat_team_and_expose_lost_win(self):
        a=self.game();b=self.game('on')
        for directory,winner in ((a,'3'),(b,'2')):
            rows=subject.table(directory,'games.csv')
            rows[0].update(category='team',sides='harkonnen:3:QB ordos:3:QH atreides:2:QE',outcome='decided',winner_team=winner)
            csv_file(directory/'games.csv',rows)
        pair=subject.pair_records(subject.game_evidence(a),subject.game_evidence(b))[0]
        self.assertFalse(pair['issues'])
        self.assertEqual(pair['outcome_change'],'won->lost')
        self.assertIn('lost-baseline-win',pair['flags'])
        self.assertEqual(pair['after']['observer_result'],dict(outcome='decided',winner_team='2'))

    def test_observer_unknown_winner_fails_and_timeout_stays_timeout(self):
        directory=self.game();rows=subject.table(directory,'games.csv')
        rows[0].update(category='cy',sides='harkonnen:1:QB atreides:2:QE',outcome='decided',winner_team='9')
        csv_file(directory/'games.csv',rows)
        self.assertIn('engine-error-or-unfinished',subject.game_evidence(directory)[self.gid]['issues'])
        rows[0].update(outcome='time_limit',winner_team='0');csv_file(directory/'games.csv',rows)
        game=subject.game_evidence(directory)[self.gid]
        self.assertEqual(game['outcome'],'time_limit');self.assertFalse(game['issues'])

    def test_control_receipt_binds_real_input_bytes_and_requires_reviews(self):
        a=self.game();b=self.game('on')
        first=subject.control_report(a,b,self.control_schedule(),'campaign-all-maps','candidate',control_config='control')
        self.assertTrue(first['complete']);self.assertEqual(first['pending_reviews'],[self.gid])
        rows=subject.table(b,'games.csv');rows[0]['outcome']='won';csv_file(b/'games.csv',rows)
        second=subject.control_report(a,b,self.control_schedule(),'campaign-all-maps','candidate',control_config='control')
        self.assertNotEqual(first['input_hashes'],second['input_hashes'])
        self.assertNotEqual(first['games'][0]['review_key'],second['games'][0]['review_key'])

    def test_historical_allows_engine_content_change_but_not_map_or_options(self):
        a=subject.game_evidence(self.game());b=subject.game_evidence(self.game('on'))
        a[self.gid]['source']='b'*40;a[self.gid]['setup']['content_hash']='older'
        pair=subject.pair_records(a,b,historical=True)[0]
        self.assertFalse(pair['issues']);self.assertIn('content_hash',pair['version_differences'])
        a[self.gid]['setup']['options_hash']='wrong'
        self.assertIn('missing-or-changed-world-identity',subject.pair_records(a,b,historical=True)[0]['issues'])

    def test_forged_map_labels_do_not_supply_66_map_coverage(self):
        p=self.root/'campaign-all-maps.json';rows=json.loads(p.read_text())
        for r in rows:r['map']='harkonnen level 8 mission 21'
        p.write_text(json.dumps(rows))
        with self.assertRaisesRegex(ValueError,'66'):subject.validate_manifest(self.m,self.root)

    def test_unhashed_play_style_not_accepted_as_research_switch(self):
        (self.root/'tests/ai/play-styles/candidate.ini').write_text('[Play Style]\nMinWaveUnits=3\n')
        with self.assertRaisesRegex(ValueError,'hashed'):subject.validate_manifest(self.m,self.root)

    def test_self_consistent_fabricated_control_certificate_is_rejected(self):
        a=self.game();b=self.game('on');provenance=subject.validate_manifest(self.m,self.root)
        certificate=dict(schema='campaign-research-control/1',kind='campaign-all-maps',source_revision='a'*40,
            candidate_config='candidate',schedule_sha256=provenance['control_plans']['campaign-all-maps']['sha256'],
            complete=True,pending_reviews=[],games=[{'id':'invented'}])
        certificate['games_sha256']=subject.digest(certificate['games'])
        file=self.root/'forged.json';file.write_text(json.dumps(certificate))
        result=subject.experiment_report(self.path,'pilot',a,b,{'controls':{'campaign-all-maps':str(file)}})
        self.assertFalse(result['controls']['campaign-all-maps'])
        self.assertFalse(result['facts']['promotion_ready'])

    def test_duplicate_case_rejected(self):
        p=self.root/'pilot.json';d=json.loads(p.read_text());d['cases']*=2;p.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,'Duplicate'):subject.cases(p)


if __name__=='__main__':unittest.main()
