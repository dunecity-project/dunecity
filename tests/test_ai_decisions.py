import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('ai_decisions', Path(__file__).parents[1] / 'scripts/ai-decisions.py')
ai = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ai)


class ImportTest(unittest.TestCase):
    def test_shared_capital_views_link_all_charges_without_rejected_orders(self):
        with ai.connect(':memory:') as db:
            def add(session, seq, kind, data):
                ai.insert(db,dict(schema_version=1,session=session,seq=seq,cycle=100,
                                 house=0,player=2,event=kind,data=data),'fixture')
            for session in ('city', 'vanilla'):
                add(session,1,'capital_plan',dict(mode=session,spendable=2000,selected=0,candidates={
                    '0':dict(builder=10,item=31,kind='economy',price=300,foundation_cost=0,
                             total_cost_with_power=300,score=2000,reason='marginal_income',affordable=1),
                    '1':dict(builder=11,item=33,kind='military',price=450,score=800,reason='military_shortfall')}))
                add(session,2,'production_order',dict(capital_plan=1,builder=10,item=31,item_name='Harvester',
                    quoted_price=300,accepted=1,rule='shared_capital_priority'))
                add(session,3,'production_order',dict(capital_plan=1,builder=11,item=33,item_name='Launcher',
                    quoted_price=450,accepted=0,rule='unit_mix'))
                add(session,4,'capital_upgrade',dict(capital_plan=1,builder=11,price=200,accepted=1,reason='technology_unlock'))
                add(session,5,'capital_road_batch',dict(capital_plan=1,builder=12,cost=20,count=2,price=10))
                add(session,6,'tech_research_started',dict(lab=13,cost=600,to=2))
                add(session,7,'research_started',dict(capital_plan=1,lab=13,cost=600,to=2))
                add(session,8,'capital_outcome',dict(plan=1,ordered_cost=1120,remaining_planning_cash=880))
            self.assertEqual(db.execute('select count(*) from capital_candidates where selected=1').fetchone(),(2,))
            self.assertEqual(db.execute('select session,sum(cost) from capital_orders group by session').fetchall(),
                             [('city',1120),('vanilla',1120)])
            self.assertEqual(db.execute('select count(*) from capital_plans p join capital_outcomes o '
                                        'using(session,plan) where p.spendable=o.ordered_cost+o.remaining_cash').fetchone(),(2,))
            self.assertEqual(db.execute("select session,builder,item,cost from capital_orders "
                                        "where kind='research' order by session").fetchall(),
                             [('city',13,88,600),('vanilla',13,88,600)])

    def test_spending_audit_counts_ai_research_once_without_house_event_duplication(self):
        import subprocess
        import sys
        with tempfile.TemporaryDirectory() as tmp:
            events = Path(tmp) / 'research.jsonl'
            result = Path(tmp) / 'research-report.json'
            plan = dict(mode='city',spendable=2000,forecast_net_income=4000,forecast_funding=6000,
                        military_value=3000,military_target=10000,workers=4,worker_target=4,
                        spice_share=20000,selected=-1,candidates={})
            rows = [('capital_plan',plan),
                    ('tech_research_started',dict(lab=13,cost=600,to=2)),
                    ('research_started',dict(capital_plan=1,lab=13,cost=600,to=2)),
                    ('capital_outcome',dict(plan=1,ordered_cost=600,remaining_planning_cash=1400)),
                    ('session_end',{})]
            events.write_text(''.join(json.dumps(dict(schema_version=1,session='research',
                seq=seq,cycle=100,house=0,player=2,event=kind,data=data))+'\n'
                for seq,(kind,data) in enumerate(rows,1)))
            audit = subprocess.run([sys.executable,str(Path(__file__).parent / 'ai/report-spending.py'),
                                    str(events),'--output',str(result),'--check'],
                                   capture_output=True,text=True)
            self.assertEqual(audit.returncode,0,audit.stderr or audit.stdout)
            report = json.loads(result.read_text())
            self.assertEqual(report['violations'],[])
            self.assertEqual(report['houses']['0']['spending']['Tech research'],600)

    def test_combat_rewards_and_allocation_are_queryable_without_summing_snapshots(self):
        reward = dict(item_name='Tank',damage_value_milli=40000,kill_bonus_milli=120000,
                      conversion_value_milli=0,reward_milli=160000,hits=1,unit_killing_blows=1,
                      raw_damage=100,loss_count=2,lost_value=600)
        with ai.connect(':memory:') as db:
            for seq,kind,data in [(1,'state_snapshot',{'house_comparison':{'1':{'combat_rewards':{'33':reward}}}}),
                                  (2,'game_summary',{'houses':{'1':{'combat_rewards':{'33':reward}}}}),
                                  (3,'unit_mix',{'basis':'value_per_loss','tech_level':8,'performance_exponent_milli':1500,'mix_inputs':{
                                      '33':dict(available=1,opening_bps=500,target_bps=1500,
                                                damage=100,reward_milli=160000,kill_bonus_milli=120000,
                                                lost_value=600,score=178,allocation_weight=25000)}})]:
                ai.insert(db,dict(schema_version=1,session='reward',seq=seq,cycle=seq,house=1,
                                 player=18,event=kind,data=data),'fixture')
            self.assertEqual(db.execute('select house,item,damage_value,kill_bonus,reward,unit_killing_blows '
                                        'from combat_reward_final').fetchall(),[(1,33,40.0,120.0,160.0,1)])
            self.assertEqual(db.execute('select count(*) from combat_reward_samples').fetchone(),(2,))
            self.assertEqual(db.execute('select opening_bps,target_bps,reward,score from unit_allocation').fetchone(),
                             (500,1500,160.0,178))
            self.assertEqual(db.execute('select performance_exponent_milli,allocation_weight from unit_allocation').fetchone(),
                             (1500,25000))
            ai.insert(db,dict(schema_version=1,session='old',seq=1,cycle=1,house=1,player=18,
                             event='unit_mix',data={'mix_inputs':{'33':{'damage':100}}}),'fixture')
            self.assertEqual(db.execute("select reward from unit_allocation where session='old'").fetchone(),(None,))

    def test_raid_results_attribute_members_and_do_not_sum_progress(self):
        with ai.connect(':memory:') as db:
            def add(seq,kind,data):
                ai.insert(db,dict(schema_version=1,session='raid',seq=seq,cycle=seq*100,
                                 house=1,player=18,event=kind,data=data),'fixture')
            add(1,'harvester_raid',dict(target=90,members={'42':dict(item=33,price=300)}))
            progress=dict(raid_id=1,target=90,outcome='active',elapsed_cycles=100,
                          transit_cycles=60,engagement_cycles=40,survivors=1,losses=0,captured=0,
                          lost_value=0,damage_value_milli=300000,kill_bonus_milli=60000,reward_milli=360000,
                          members={'42':dict(lost=False,captured=False,reward_milli=360000)})
            add(2,'harvester_raid_progress',progress)
            add(3,'lethal_damage',dict(object=90))
            add(4,'harvester_raid_outcome',dict(progress,outcome='target_killed_by_raid'))
            self.assertEqual(db.execute('select count(*) from raid_samples').fetchone(),(2,))
            self.assertEqual(db.execute('select raid_id,reward,target_death_confirmed from raid_outcomes').fetchall(),
                             [(1,360.0,1)])
            self.assertEqual(db.execute('select object,price from raid_members').fetchall(),[(42,300)])
            self.assertEqual(db.execute('select object,reward,lost from raid_member_results').fetchall(),[(42,360.0,0)])
            add(5,'harvester_raid',dict(target=91,members={}))
            add(6,'harvester_raid_outcome',dict(progress,raid_id=5,target=91,outcome='target_lost_contact'))
            self.assertEqual(db.execute('select target_death_confirmed from raid_outcomes where raid_id=5').fetchone(),(0,))
            add(7,'harvester_strike',dict(target=92,members={'43':dict(item=34,price=900)}))
            add(8,'harvester_strike_outcome',dict(progress,raid_id=7,target=92,outcome='target_killed_by_strike'))
            self.assertEqual(db.execute('select operation,reward from raid_outcomes where raid_id=7').fetchone(),
                             ('main_force_strike',360.0))
            self.assertEqual(db.execute('select operation,price from raid_members where raid_id=7').fetchone(),
                             ('main_force_strike',900))

    def test_repeat_import_and_live_tail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'events.jsonl'
            row = dict(schema_version=1, session='test', seq=1, cycle=30, house=6, player=2,
                       event='production_order', data=dict(item=21, item_name='Commercial Zone', accepted=1,
                       state=dict(res_demand=319, com_demand=1500, credits=18000)))
            path.write_text(json.dumps(row)+'\n'+ '{"schema_version":')
            with ai.connect(':memory:') as db:
                self.assertEqual(ai.import_jsonl(db, path), (1, 0, 1))
                self.assertEqual(ai.import_jsonl(db, path), (0, 0, 1))
                self.assertEqual(db.execute('select item_name,credits from production').fetchone(), ('Commercial Zone',18000))
                path.write_text(json.dumps(row)+'\n'+json.dumps(dict(row, seq=2))+'\n')
                self.assertEqual(ai.import_jsonl(db, path), (1,0,0))

    def test_malformed_complete_records_are_reported_and_skipped(self):
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad.jsonl'
            path.write_text('[]\n{"schema_version":2}\n')
            errors = io.StringIO()
            with ai.connect(':memory:') as db, contextlib.redirect_stderr(errors):
                self.assertEqual(ai.import_jsonl(db, path), (0, 2, 0))
            self.assertIn('event must be a JSON object', errors.getvalue())
            self.assertIn('unsupported schema version', errors.getvalue())

    def test_audit_checks_session_sequence_and_same_player_references(self):
        with ai.connect(':memory:') as db:
            def event(seq, kind, data, house=1):
                ai.insert(db, dict(schema_version=1, session='s', seq=seq, cycle=seq,
                    house=house, player=17, event=kind, data=data), 'fixture')
            event(1, 'session_start', {})
            event(2, 'state_snapshot', {})
            event(3, 'production_order', {'state_id': 2})
            self.assertEqual(ai.audit(db)['issues'], [])
            event(4, 'production_order', {'state_id': 2}, house=3)
            self.assertIn('invalid state_id', ai.audit(db)['issues'][0])
            event(6, 'production_order', {'state_id': 2})
            self.assertTrue(any('non-contiguous' in issue for issue in ai.audit(db)['issues']))

    def test_audit_requires_a_reason_for_skipping_higher_ranked_zone(self):
        with ai.connect(':memory:') as db:
            for seq, event, data in [(1, 'session_start', {}), (2, 'zone_evaluation', {
                'selected': 20, 'candidates': {'20': {'rank': 1}, '21': {'rank': 0}},
                'evaluated': {'20': 'selected', '21': 'lower_rank_not_evaluated'}})]:
                ai.insert(db, dict(schema_version=1, session='s', seq=seq, house=1, player=17,
                                   event=event, data=data), 'fixture')
            self.assertTrue(any('skipped higher-ranked' in issue for issue in ai.audit(db)['issues']))

    def test_audit_detects_ghost_power_without_requiring_new_fields_in_old_logs(self):
        with ai.connect(':memory:') as db:
            for seq, event, data in [(1, 'session_start', {}),
                    (2, 'state_snapshot', {}),
                    (3, 'state_snapshot', {'power_accounting': {'difference': 0}}),
                    (4, 'state_snapshot', {'power_accounting': {'difference': 1000}})]:
                ai.insert(db, dict(schema_version=1, session='s', seq=seq, house=3, player=49,
                                   event=event, data=data), 'fixture')
            self.assertEqual(len(ai.audit(db)['issues']), 1)
            self.assertIn('power accounting mismatch in 1 snapshots (max 1000)', ai.audit(db)['issues'][0])

    def test_conflicting_reimport_cannot_silently_replace_evidence(self):
        with ai.connect(':memory:') as db:
            row=dict(schema_version=1,session='s',seq=1,cycle=0,house=-1,player=-1,event='session_start',data={})
            ai.insert(db,row,'fixture')
            ai.insert(db,row,'fixture')
            with self.assertRaisesRegex(ValueError,'conflicting event'):
                ai.insert(db,dict(row,data={'seed':123}),'fixture')
            self.assertEqual(db.execute('select count(*) from events').fetchone()[0],1)

    def test_match_report_uses_gameplay_duration_and_distinguishes_inherited_hunters(self):
        with ai.connect(':memory:') as db:
            snapshot=dict(actual={'4':2},queued={},heavy_busy=1,state=dict(credits=30000,military=80000))
            records=[('session_start',0,{'cycles_per_30_seconds':1875}),
                     ('state_snapshot',3750,snapshot),('attack_launched',4000,{'units_sent':100,'force_value':0}),
                     ('session_end',0,{})]
            for seq,(event,cycle,data) in enumerate(records,1):
                ai.insert(db,dict(schema_version=1,session='s',seq=seq,cycle=cycle,house=1,player=17,event=event,data=data),'fixture')
            report=ai.match_report(db)[0]
            self.assertEqual(report['last_cycle'],4000)
            self.assertTrue(any('stale' in warning for warning in report['warnings']))
            self.assertIsNone(report['final_summary'])
            self.assertEqual(report['players'][0]['phases'][0]['busy_factory_fraction'],0.5)
            self.assertEqual(report['players'][0]['attacks']['positive_new_force'],0)

    def test_legacy_preserves_evidence_without_inventing_ticks(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'before.log'
            path.write_text('qBotBrutal (Neutral):   CITY-ZONE: Building Residential Zone (R:191 C:56 I:65 valves=R+319 C+1500 I+1500 surplus=2776)\n')
            with ai.connect(':memory:') as db:
                self.assertEqual(ai.import_legacy(db,path)[0],1)
                self.assertEqual(ai.import_legacy(db,path)[0],0)
                self.assertEqual(db.execute('select cycle from events').fetchone(),(None,))
                data=json.loads(db.execute('select data from events').fetchone()[0])
                self.assertEqual(data['res_demand'],319)
                self.assertTrue(data['observed_only'])

class PerformanceAnalyticsTest(unittest.TestCase):
    def test_performance_views_preserve_units_houses_and_idempotence(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'events.jsonl'
            metric=dict(scope='ai.build',house=4,item=-1,unit='us',count=3,sum=350000,
                        max=300000,max_cycle=20,over_33ms=2,over_100ms=1,over_250ms=1)
            row=dict(schema_version=1,session='performance',seq=1,cycle=30,house=-1,player=-1,
                     event='performance_window',data=dict(start_cycle=0,elapsed_us=5000000,
                     worst_frame_cycle=20,worst_frame_us=350000,worst_frame={'worst_house':4},
                     metrics={'0':metric}))
            path.write_text(json.dumps(row)+'\n')
            with ai.connect(':memory:') as db:
                self.assertEqual(ai.import_jsonl(db,path)[0],1)
                self.assertEqual(ai.import_jsonl(db,path)[0],0)
                self.assertEqual(db.execute('select scope,house,unit,samples,total,maximum,max_cycle,over_100ms '
                                            'from performance_metrics').fetchall(),
                                 [('ai.build',4,'us',3,350000,300000,20,1)])
                self.assertEqual(db.execute('select wall_seconds,worst_frame_ms,worst_frame_cycle '
                                            'from performance_windows').fetchall(),[(5.0,350.0,20)])

class CityAnalyticsTest(unittest.TestCase):
    def test_city_views_keep_local_stats_and_causal_labels(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'events.jsonl'
            base = dict(schema_version=1,telemetry_version=4,session='city',cycle=7500,house=0,player=-1)
            rows = [dict(base,seq=1,event='city_building_snapshot',data={'buildings':{'42':{
                'item':20,'x':8,'y':9,'population':24,'population_density':110,'land_value':77,'crime':201}}}),
                dict(base,seq=2,event='city_level_changed',data={'object':42,'old_level':2,'new_level':1,
                'outcome':'decline','reason':'power_shortage','powered':0,'crime':201})]
            path.write_text(''.join(json.dumps(r)+'\n' for r in rows))
            with ai.connect(':memory:') as db:
                self.assertEqual(ai.import_jsonl(db,path),(2,0,0))
                self.assertEqual(db.execute('SELECT object,population,population_density,land_value,crime FROM city_buildings').fetchone(),(42,24,110,77,201))
                self.assertEqual(db.execute('SELECT object,reason,powered FROM city_growth').fetchone(),(42,'power_shortage',0))
                self.assertEqual(ai.import_jsonl(db,path),(0,0,0))

if __name__ == '__main__':
    unittest.main()
