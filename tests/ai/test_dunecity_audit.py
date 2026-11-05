"""Read-only audit contract fixtures; never build, run games, or claim AI evidence."""
import ast
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch


HARNESS = Path(__file__).with_name('dunecity-audit.py')
# Use the same top-level args boundary as the cluster harness loader.
tree = ast.parse(HARNESS.read_text(encoding='utf-8'), str(HARNESS))
prefix = []
for node in tree.body:
    if (isinstance(node, ast.Assign) and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'args'):
        break
    prefix.append(node)
else:
    raise AssertionError('missing queue AST boundary')
audit = {'__file__': str(HARNESS), '__name__': 'cluster_harness'}
exec(compile(ast.Module(body=prefix, type_ignores=[]), str(HARNESS), 'exec'), audit)


HOUSES = [dict(house='0', team='1'), dict(house='1', team='2'), dict(house='2', team='1')]


def callbacks():
    records = [
        ('session_start', -1, 0, {}),
        ('lethal_damage', 1, 50, dict(object=10, item=35, attacker=100, attacker_house=0)),
        ('lethal_damage', 0, 60, dict(object=11, item=35, attacker=101, attacker_house=2)),
        ('lethal_damage', 0, 70, dict(object=12, item=80, attacker=102, attacker_house=1)),
        ('lethal_damage', 1, 80, dict(object=13, item=35, attacker=0, attacker_house=-1)),
        ('game_summary', -1, 100, dict(houses={house['house']: house for house in HOUSES})),
        ('session_end', -1, 100, {}),
    ]
    for name, _, _, data in records:
        if name == 'lethal_damage' and data['attacker_house'] >= 0:
            data.update(attacker_item=audit['TROOPER_ITEM'], attacker_source=data['attacker'],
                        attacker_firing_house=data['attacker_house'], attacker_attribution='snapshot')
    return [dict(schema_version=1, session='fixture', seq=seq, cycle=cycle, house=house, event=name, data=data)
            for seq, (name, house, cycle, data) in enumerate(records, 1)]


def known_callbacks():
    events = callbacks()
    events[4]['data'].update(attacker=103, attacker_house=1, attacker_item=32, attacker_source=103,
                             attacker_firing_house=1, attacker_attribution='direct')
    return events


def write_capture(directory, events):
    path = directory / 'profile/ai-decisions/fixture/events.jsonl'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(''.join(json.dumps(event) + '\n' for event in events), encoding='utf-8')


def game_row(style, **extra):
    row = dict(id='matrix/fixture', seed='9101', size='96', spice_level='none', format='1v1',
               difficulty='hard-v-brutal', variant=style, base='medium', army='2', city='0', limit='30',
               style=style, initial_tech='9', sides='harkonnen:1:hard,atreides:2:brutal', error='', probe_sha256='same-probe',
               source_commit='same-source', source_fingerprint='same-fingerprint', source_fingerprint_error='',
               map_hash='same-map', options_hash='same-options', content_hash='same-content',
               object_data_hash='same-object-data', air_kill_telemetry='complete',
               air_kill_attribution='complete', air_kill_unknown_attribution='0')
    row.update(extra)
    return row


def house_row(house='0', difficulty='hard', **extra):
    row = dict(id='matrix/fixture', house=house, team=str(int(house) + 1), difficulty=difficulty,
               spice_level='none', economy_stall='0', eligible_economy_stall='1', bankrupt_queue_empty='0',
               eligible_bankrupt_queue_empty='1', missing_starport='0', eligible_missing_starport='1',
               attack_count='3', attack_telemetry='1', air_kill_telemetry='complete', kill_value='1000',
               air_kill_attribution_complete='1', air_kill_unknown_attribution='0')
    row.update({field: '0' for field in audit['TROOPER_FIELDS'] + audit['AIR_COUNT_FIELDS']})
    row.update(extra)
    return row


class DuneCityAuditTests(unittest.TestCase):
    def test_initial_technology_changes_the_real_skirmish_without_colliding_with_default_ids(self):
        options = ['--sizes', '96', '--spice', 'medium', '--formats', '1v1',
                   '--difficulties', 'hard', '--seeds', '9101']
        default = audit['schedule'](audit['parser'].parse_args(options))[0]
        explicit = audit['schedule'](audit['parser'].parse_args(options + ['--tech', '9']))[0]
        low = audit['schedule'](audit['parser'].parse_args(options + ['--tech', '1']))[0]
        self.assertEqual(default, explicit)
        self.assertEqual(default['id'], 'matrix/medium-a2-c0/96/medium/1v1/hard/30m/9101')
        self.assertEqual(low['id'], default['id'] + '/t1')
        self.assertIn('tech=1,', low['skirmish'])
        self.assertIn('tech=9,', default['skirmish'])
        self.assertEqual(audit['metadata'](low)['initial_tech'], 1)
        self.assertEqual(audit['metadata'](default)['initial_tech'], 9)

    def test_optional_six_house_audit_covers_every_faction_without_changing_defaults(self):
        args = audit['parser'].parse_args([
            '--sizes', '128', '--spice', 'medium', '--formats', 'ffa6',
            '--difficulties', 'hard', '--seeds', '9101', '148680876'])
        games = audit['schedule'](args)
        self.assertEqual(len(games), 2)
        for game in games:
            self.assertEqual({side[0] for side in game['sides']}, set(audit['HOUSES']))
            self.assertEqual({side[1] for side in game['sides']}, set(range(1, 7)))
            self.assertEqual({side[2] for side in game['sides']}, {'hard'})
        self.assertNotEqual(games[0]['sides'][0][0], games[1]['sides'][0][0])
        self.assertEqual(audit['parser'].parse_args([]).formats, ['1v1', '2v2', 'ffa4'])

    def test_classic_content_has_distinct_ids_and_reuses_the_real_scenario_route(self):
        common = ['--sizes', '96', '--spice', 'medium', '--formats', '1v1',
                  '--difficulties', 'medium', 'hard', 'brutal', '--seeds', '9101', '148680876']
        city = audit['schedule'](audit['parser'].parse_args(common))
        self.assertTrue(all(game['id'].startswith('matrix/') for game in city))
        for mod in ('Dune2R', 'vanilla'):
            args = audit['parser'].parse_args(common + ['--mod', mod])
            games = audit['schedule'](args)
            self.assertEqual(len(games), 6)
            self.assertTrue(all(game['id'].startswith(mod + '/matrix/') for game in games))
            command = audit['command'](args, Path('unused-probe'), games[0], Path('unused-output'))
            self.assertEqual(command[command.index('--mod') + 1], mod)
            self.assertEqual([game['skirmish'] for game in games], [game['skirmish'] for game in city])

    def test_composition_uses_current_and_queued_replacement_values_not_the_cap(self):
        lines = []
        for slot in range(12):
            data = dict(minute=10, cycle=37500, house=0, slot=slot, item=41,
                        live_count=2 if slot == 0 else 3 if slot == 1 else 0,
                        live_value=1000 if slot == 0 else 500 if slot == 1 else 0,
                        queued_count=int(slot == 0), queued_value=500 if slot == 0 else 0,
                        target_bps=4000 if slot == 0 else 6000 if slot == 1 else 0 if slot < 10 else -1,
                        allocation_ready=1, allocation_cycle=37000, army_target=50000)
            lines.append('DUNECITY_COMPOSITION: ' + ' '.join(f'{key}={value}' for key, value in data.items()))
        rows = audit['composition_rows'](lines)
        tank = rows[0]
        self.assertEqual((tank['committed_count'], tank['committed_value']), (3, 1500))
        self.assertEqual(tank['committed_count_bps'], 5000)
        self.assertEqual(tank['committed_value_bps'], 7500)
        self.assertEqual(tank['value_target_delta_bps'], 3500)
        self.assertAlmostEqual(tank['live_value_bps'], 6666.667)
        self.assertEqual(rows[10]['target_bps'], '')
        self.assertEqual(rows[10]['committed_value_bps'], '')
        partial = audit['composition_rows'](lines[:-1])
        self.assertTrue(all(row['committed_value_bps'] == '' for row in partial))

    def test_accepted_spending_partitions_quotes_and_keeps_diagnostic_overlays_separate(self):
        def order(item, **data):
            return audit['accepted_order'](dict(session='fixture', seq=1, cycle=25, house=0,
                event='production_order', data=dict(item=item, accepted=1, quoted_price=100,
                    queue_before=0, queue_after=1, **data)))
        self.assertEqual(order(44)['bucket'], 'military')
        self.assertEqual(order(44)['cheap_unit'], 1)
        self.assertEqual(order(41)['cheap_unit'], 0)  # A Tank is not cheap infantry/light spam.
        self.assertEqual((order(4)['bucket'], order(4)['tech']), ('production', 1))
        self.assertEqual((order(26)['bucket'], order(26)['city_spend']), ('economy', 1))  # Police, not a turret.
        self.assertEqual((order(12)['bucket'], order(12)['city_spend']), ('military', 0))
        self.assertEqual((order(31)['bucket'], order(31)['city_spend']), ('economy', 0))
        self.assertEqual(order(14, spending_bucket='production')['bucket'], 'production')  # Parent project's slab.
        self.assertEqual((order(14, spending_bucket='production')['tech'],
                          order(14, spending_bucket='production')['city_spend']), (1, 0))
        path_order = order(36, rule='light_backlog', composition_basis='existing_and_queued',
                           composition_before={'36': {'committed_value': 200, 'target_bps': 1000, 'available': 1}},
                           composition={'36': {'committed_value': 400, 'target_bps': 1000, 'available': 1}},
                           state={'military': 1000, 'military_limit': 50000, 'military_budget': 2000})
        self.assertEqual((path_order['rule'], path_order['composition_basis']), ('light_backlog', 'existing_and_queued'))
        self.assertEqual(path_order['military_headroom'], 1000)
        self.assertEqual(path_order['state_cycle'], 25)
        self.assertEqual(json.loads(path_order['composition_before'])['36']['committed_value'], 200)
        self.assertEqual(json.loads(path_order['composition_after'])['36']['committed_value'], 400)
        path = audit['cheap_path_rows']([path_order, path_order], True)[0]
        self.assertEqual((path['order_count'], path['accepted_credits']), (2, 200))
        limited_path = audit['cheap_path_rows']([path_order], False)[0]
        self.assertEqual(limited_path['accepted_credits'], '')
        self.assertEqual(limited_path['observed_priced_credits'], 100)

    def test_tech_sidecars_have_unique_column_names(self):
        for filename, fields in audit['TECH_TABLES']:
            with self.subTest(filename=filename):
                self.assertEqual(len(fields), len(set(fields)))

    def test_city_spending_does_not_overwrite_starting_city_in_csv(self):
        args = audit['parser'].parse_args(['--city', '0'])
        game = audit['schedule'](args)[0]
        order = audit['accepted_order'](dict(session='fixture', seq=1, cycle=25, house=0,
            event='production_order', data=dict(item=26, accepted=1, quoted_price=100,
                queue_before=0, queue_after=1)))
        row = dict(audit['metadata'](game), mod='dunecity', **order, capture_complete=1)
        output = io.StringIO()
        writer = csv.DictWriter(output, fieldnames=audit['TECH_ORDER_FIELDS'])
        writer.writeheader()
        writer.writerow(row)
        output.seek(0)
        captured = next(csv.DictReader(output))
        self.assertEqual(captured['city'], '0')
        self.assertEqual(captured['city_spend'], '1')

    def test_simple_policy_context_does_not_imply_income_bucket_allocation(self):
        plan = dict(session='fixture', seq=1, cycle=0, house=0, event='tech_savings_plan',
            data=dict(feasible=0, tech_savings=1, savings_policy='installments', income_buckets=0,
                      tech_funding=1, ledger_initialized=0, production_bps=3000))
        result = audit['tech_audit']([], [dict(house='0', cycle='20000')],
            [dict(house='0', alive='1', eliminated_s='-1')], dict(status='complete', decisions=[plan]))
        captured = result['tech-opportunities.csv'][0]
        self.assertEqual((captured['tech_savings'], captured['savings_policy'], captured['income_buckets'],
                          captured['tech_funding'], captured['ledger_initialized']), (1, 'installments', 0, 1, 0))
        self.assertEqual(captured['production_bps'], 3000)  # Raw context, not an active allocation.
        legacy = dict(plan, data=dict(feasible=0, tech_savings=0))
        historical = audit['tech_audit']([], [dict(house='0', cycle='20000')],
            [dict(house='0', alive='1', eliminated_s='-1')], dict(status='complete', decisions=[legacy]))
        row = historical['tech-opportunities.csv'][0]
        self.assertEqual((row['savings_policy'], row['income_buckets'], row['tech_funding']), ('', '', ''))

    def test_four_minute_quote_diversion_censors_short_dead_and_incomplete_windows(self):
        def event(name, seq, cycle, **data):
            return dict(session='fixture', house=0, event=name, seq=seq, cycle=cycle, data=data)
        decisions = [
            event('tech_savings_plan', 1, 0, feasible=1, candidate_kind='structure', item=6, builder=20,
                  cost=800, cash=500, committed_cost=100, forecast_net_income=500, horizon_cycles=15000,
                  working_reserve=0, stable=1, tech_savings=0, production_bps=3000, economy_bps=3500, military_bps=3500),
            event('production_order', 2, 25, item=44, builder=21, accepted=1, quoted_price=200,
                  queue_before=0, queue_after=1),
            event('production_order', 3, 30, item=20, builder=20, accepted=1, quoted_price=100,
                  queue_before=0, queue_after=1),
            event('capital_upgrade', 4, 40, builder=22, accepted=1, price=200),
            event('city_yard_upgrade', 5, 40, builder=22, accepted=1, cost=200),  # Duplicate diagnostic.
            event('capital_road_batch', 6, 50, builder=20, count=2, price=10, cost=20),
            event('production_order', 7, 60, item=35, builder=23, accepted=0, quoted_price=700),
        ]
        marker = ('DUNECITY_TECH_SAMPLE: minute=5 cycle=20000 house=0 credits=100 military_live_count=4 '
                  'military_live_value=1200 military_queued_count=2 military_queued_value=600')
        bucket = ('DUNECITY_SPENDING_BUCKETS: minute=5 cycle=20000 house=0 bucket_initialized=1 '
                  'bucket_last_wealth=100 bucket_production_credits_x10000=-2000000 '
                  'bucket_economy_credits_x10000=150000 bucket_military_credits_x10000=0')
        sample = dict(house='0', cycle='20000', structures='4', army_target='50000')
        final = [dict(house='0', alive='1', eliminated_s='-1')]
        capture = dict(status='complete', decisions=decisions)
        result = audit['tech_audit']([marker, bucket], [sample], final, capture)
        plan = result['tech-opportunities.csv'][0]
        self.assertEqual((plan['fully_observed_4m'], plan['affordable_without_cheap_4m'],
                          plan['accepted_cheap_4m'], plan['quoted_diversion_4m']), (1, 1, 200, 1))
        row = result['tech-houses.csv'][0]
        self.assertEqual((row['accepted_production'], row['accepted_economy'], row['accepted_military']),
                         (200, 120, 200))
        self.assertEqual((row['accepted_cheap_unit'], row['accepted_tech'], row['accepted_city']), (200, 200, 120))
        self.assertEqual(len(result['tech-orders.csv']), 4)
        point = result['tech-samples.csv'][0]
        self.assertEqual((point['mean_military_unit_cost'], point['mean_committed_unit_cost']), (300, 300))
        self.assertEqual(point['bucket_production_credits_x10000'], '-2000000')  # No lost debt/precision.
        self.assertEqual(point['structures'], '4')
        for ending in ([dict(house='0', alive='0', eliminated_s='120')], final):
            samples = [dict(sample, cycle='10000')] if ending is final else [sample]
            censored = audit['tech_audit']([], samples, ending, capture)['tech-opportunities.csv'][0]
            self.assertEqual(censored['fully_observed_4m'], 0)
            self.assertEqual(censored['quoted_diversion_4m'], '')
        limited = audit['tech_audit']([marker], [sample], final, dict(capture, status='capture_limit'))
        self.assertEqual(limited['tech-houses.csv'][0]['accepted_cheap_unit'], '')
        self.assertEqual(limited['tech-opportunities.csv'][0]['fully_observed_4m'], '')
        purchased = decisions + [event('production_order', 8, 100, item=6, builder=20, accepted=1,
                                       quoted_price=800, queue_before=0, queue_after=1)]
        observed = audit['tech_audit']([], [sample], final, dict(capture, decisions=purchased))
        self.assertEqual(observed['tech-opportunities.csv'][0]['quoted_diversion_4m'], 0)
        self.assertEqual(observed['tech-opportunities.csv'][0]['candidate_ordered_4m'], 1)

    def test_missing_quote_is_unmeasured_and_absent_tech_sidecars_do_not_break_old_reports(self):
        event = dict(session='fixture', seq=1, cycle=25, house=0, event='production_order',
                     data=dict(item=44, accepted=1, queue_before=0, queue_after=1))
        result = audit['tech_audit']([], [dict(house='0', cycle='20000')],
            [dict(house='0', alive='1', eliminated_s='-1')], dict(status='complete', decisions=[event]))
        self.assertEqual(result['tech-houses.csv'][0]['accepted_military'], '')
        self.assertEqual(result['tech-orders.csv'][0]['price'], '')
        with tempfile.TemporaryDirectory() as temporary:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                audit['tech_report']([Path(temporary)])
            self.assertIn('unmeasured', output.getvalue())
        self.assertNotIn('mean_military_unit_cost', audit['SAMPLE_FIELDS'])  # Original schema stays unchanged.
        self.assertIn('mean_military_unit_cost', audit['TECH_SAMPLE_FIELDS'])

    def test_capture_allowance_passes_through_the_existing_probe_option(self):
        args = audit['parser'].parse_args(['--attack-events', '--capture-mib', '512'])
        game = audit['schedule'](args)[0]
        command = audit['command'](args, Path('unused-probe'), game, Path('unused-output'))
        self.assertEqual(command[command.index('--capture-mib') + 1], '512')
        self.assertNotIn('--no-ai-telemetry', command)
        self.assertEqual(audit['parser'].parse_args([]).capture_mib, 256)
        self.assertFalse(audit['parser'].parse_args([]).keep_telemetry)
        retained = audit['parser'].parse_args(['--keep-telemetry'])
        self.assertFalse(retained.attack_events)  # Retention itself enables the existing logger.
        self.assertNotIn('--no-ai-telemetry', audit['command'](
            retained, Path('unused-probe'), audit['schedule'](retained)[0], Path('unused-output')))
        ordinary = audit['parser'].parse_args([])
        self.assertIn('--no-ai-telemetry', audit['command'](
            ordinary, Path('unused-probe'), audit['schedule'](ordinary)[0], Path('unused-output')))

    def test_play_retains_metrics_and_actual_ladder_house_levels_without_a_game(self):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            results = top / 'results'
            results.mkdir()
            binary = top / 'not-an-executable'
            binary.write_bytes(b'reporting fixture, not a game binary')
            args = audit['parser'].parse_args([
                '--results', str(results), '--sizes', '96', '--spice', 'none', '--formats', '1v1',
                '--mode', 'ladder', '--seeds', '9101', '--style', 'trooper-support-on',
                '--keep-telemetry', '--capture-mib', '512'])
            game = audit['schedule'](args)[0]
            cycle = 30 * audit['CYCLES_PER_MINUTE']

            def simulate(command, **unused):
                out = Path(command[command.index('--output-dir') + 1])
                out.mkdir(parents=True)
                lines = ['SCENARIO_RESULT: outcome=time_limit winner_team=0 minutes=30',
                         'PROBE_TIMING: loop_ms=1', 'PROBE_DIGEST: final=1 digest=fixture']
                for name, team, _ in game['sides']:
                    house = audit['HOUSE_IDS'][name]
                    sample = dict(minute=30, cycle=cycle, house=house, team=team, alive=1, credits=10000,
                                  army_value=1000, army_units=3, harvesters=1, structures=1, factories=1,
                                  kill_value=1200, loss_value=100, power=100, need=50, population=100, tax=100,
                                  demand=1, built=1, structures_lost=0, base_hp=100, yards=1, yard_level=1,
                                  queue=0, upgrading=0, waiting=0, fronts='none', gun=1, rocket=1, radar=1,
                                  radar_built=1, starport=1, starport_built=1, heavy=1, heavy_built=1, ix=1,
                                  ix_built=1, fighters=3, moving=1, targeting=1, enemies=0, enemy_value=0,
                                  covered_enemies=0, spice=0)
                    lines.append('DUNECITY_AUDIT: ' + ' '.join(f'{key}={value}' for key, value in sample.items()))
                    support = dict.fromkeys(audit['TROOPER_FIELDS'], 0)
                    support.update(minute=30, cycle=cycle, house=house, wor=1, wor_built=2, trooper_live=5,
                                   trooper_queued=9, trooper_single_queued=1, trooper_squads_queued=1,
                                   trooper_squads5_queued=1, trooper_built=8, ornithopter_killed=17)
                    lines.append('DUNECITY_TROOPERS: ' + ' '.join(
                        f'{key}={value}' for key, value in support.items()))
                    final = {key: sample[key] for key in (
                        'house', 'team', 'alive', 'kill_value', 'loss_value', 'structures', 'army_value', 'army_units')}
                    final.update(eliminated_s=-1, sortie_minutes=0)
                    lines.append('SCENARIO_HOUSE: ' + ' '.join(f'{key}={value}' for key, value in final.items()))
                (out / 'run.log').write_text('\n'.join(lines) + '\n')
                (out / 'summary.json').write_text(json.dumps(dict(
                    sourceCommit='fixture', sourceFingerprint='fingerprint', sourceFingerprintError='',
                    attacks=[dict(house=0, cycle=37500, data=dict(members=0, value=0)),
                             dict(house=0, cycle=75000, data=dict(members=3, value=900))])))
                events = known_callbacks()
                events[2]['data'].update(attacker_house=0, attacker_firing_house=0)  # Friendly splash.
                events[-2]['cycle'] = events[-1]['cycle'] = cycle
                write_capture(out, events)
                assets = out / 'profile/mods/disposable'
                assets.mkdir(parents=True)
                (assets / 'not-diagnostic.bin').write_bytes(b'must not be retained')
                return types.SimpleNamespace(returncode=0, stdout='', stderr='')

            cleanup = audit['finish_game']

            def finish(scratch, out, failed):
                diagnostics = results / 'diagnostics' / out.name
                self.assertFalse(failed)
                self.assertEqual((diagnostics / 'run.log').read_bytes(), (out / 'run.log').read_bytes())
                self.assertEqual((diagnostics / 'summary.json').read_bytes(), (out / 'summary.json').read_bytes())
                self.assertEqual((diagnostics / 'profile/ai-decisions/fixture/events.jsonl').read_bytes(),
                                 (out / 'profile/ai-decisions/fixture/events.jsonl').read_bytes())
                self.assertFalse((diagnostics / 'profile/mods').exists())
                cleanup(scratch, out, failed)

            with patch.object(audit['subprocess'], 'run', side_effect=simulate) as launched:
                with patch.dict(audit, finish_game=finish):
                    row = audit['play'](args, top, binary, game)
            self.assertEqual(launched.call_count, 1)  # This was a fixture callback, never a process.
            self.assertEqual(row['error'], '')
            self.assertEqual(row['air_kill_telemetry'], 'complete')
            self.assertEqual(row['capture_mib'], 512)
            self.assertEqual(row['air_kill_attribution'], 'complete')
            self.assertEqual(row['air_kill_unknown_attribution'], 0)
            self.assertTrue((results / row['diagnostics'] / 'run.log').is_file())
            self.assertFalse((top / Path(row['diagnostics']).name).exists())
            houses = {house['house']: house for house in audit['read'](results / 'houses.csv')}
            self.assertEqual([houses[house]['difficulty'] for house in ('0', '1')], ['brutal', 'hard'])
            self.assertEqual(houses['0']['trooper_built'], '8')
            self.assertEqual(houses['0']['trooper_queued'], '9')
            self.assertEqual(houses['0']['ornithopter_killed'], '17')
            self.assertEqual(houses['0']['enemy_ornithopter_kills'], '1')
            self.assertEqual(houses['1']['enemy_elite_ornithopter_kills'], '1')
            self.assertEqual(houses['0']['trooper_enemy_ornithopter_kills'], '1')
            self.assertEqual(houses['1']['trooper_enemy_elite_ornithopter_kills'], '1')
            self.assertEqual(houses['0']['air_kill_attribution_complete'], '1')
            self.assertEqual([houses[house]['attack_count'] for house in ('0', '1')], ['1', '0'])
            self.assertEqual(len(audit['read'](results / 'air-kills.csv')), 4)
            kill = audit['read'](results / 'air-kills.csv')[0]
            self.assertEqual((kill['attacker_item'], kill['attacker_source'], kill['attacker_firing_house']),
                             ('44', '100', '0'))
            self.assertEqual(kill['attribution_complete'], '1')
            self.assertEqual(len(audit['read'](results / 'waves.csv')), 1)

    def test_enemy_air_kills_use_exact_attacker_house_and_victim_team(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            write_capture(out, callbacks())
            capture = audit['air_kill_capture'](out, HOUSES, 100, True)
        self.assertEqual(capture['status'], 'complete')
        self.assertEqual(capture['attribution'], 'unknown')
        self.assertEqual(capture['unknown_attribution'], 1)
        self.assertEqual([event['enemy'] for event in capture['events']], [1, 0, 1, ''])
        self.assertEqual([event['house'] for event in capture['events']], [0, 2, 1, -1])
        self.assertEqual(capture['events'][0]['attacker'], 100)
        self.assertTrue(all(event['capture_complete'] == 1 for event in capture['events']))
        samples = [dict(house='0', cycle=str(cycle), ornithopter_killed='17') for cycle in (49, 50, 100)]
        samples += [dict(house='1', cycle='100'), dict(house='2', cycle='100')]
        audit['add_air_kill_samples'](samples, capture)
        self.assertEqual([row['enemy_ornithopter_kills'] for row in samples], [0, 1, 1, 0, 0])
        self.assertEqual([row['enemy_elite_ornithopter_kills'] for row in samples], [0, 0, 0, 1, 0])
        self.assertEqual(samples[2]['ornithopter_killed'], '17')  # Raw engine credits are not enemy kills.
        self.assertEqual([row['trooper_enemy_ornithopter_kills'] for row in samples], [0, 1, '', '', ''])
        self.assertEqual([row['air_kill_attribution_complete'] for row in samples], [1, 1, 0, 0, 0])
        self.assertEqual(samples[2]['air_kill_unknown_attribution'], 1)

    def test_natural_source_and_firing_house_survive_dead_or_recaptured_shooters(self):
        events = known_callbacks()
        # A borrowed Trooper pays a Deviator bucket, and engine damage ownership
        # can differ from its firing beneficiary. Neither changes the natural source.
        events[1]['data'].update(attacker_house=1, reward_item=29)
        # Conversely a non-Trooper paying a Trooper bucket is never Trooper AA.
        events[3]['data'].update(attacker_item=32, reward_item=audit['TROOPER_ITEM'])
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            write_capture(out, events)
            capture = audit['air_kill_capture'](out, HOUSES, 100, True)
        self.assertEqual(capture['attribution'], 'complete')
        self.assertEqual(capture['unknown_attribution'], 0)
        first = capture['events'][0]
        self.assertEqual((first['house'], first['enemy'], first['attacker_firing_house'], first['source_enemy']),
                         (1, 0, 0, 1))
        self.assertEqual((first['attacker'], first['attacker_source'], first['attacker_item']), (100, 100, 44))
        samples = [dict(house=str(house), cycle='100') for house in range(3)]
        audit['add_air_kill_samples'](samples, capture)
        self.assertEqual(samples[0]['trooper_enemy_ornithopter_kills'], 1)
        self.assertEqual(samples[1]['trooper_enemy_elite_ornithopter_kills'], 0)
        self.assertTrue(all(row['air_kill_attribution_complete'] == 1 for row in samples))

    def test_old_missing_item_mismatched_source_and_unknown_house_are_not_guessed(self):
        variants = []
        legacy = known_callbacks()
        for event in legacy:
            for field in ('attacker_item', 'attacker_source', 'attacker_firing_house', 'attacker_attribution'):
                event['data'].pop(field, None)
        variants.append((legacy, 4))
        for fields in (dict(attacker_item=-1, reward_item=44), dict(attacker_source=999),
                       dict(attacker_firing_house=-2), dict(attacker_attribution='unknown')):
            events = known_callbacks()
            events[1]['data'].update(fields)
            variants.append((events, 1))
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            for events, unknown in variants:
                with self.subTest(events=events):
                    write_capture(out, events)
                    capture = audit['air_kill_capture'](out, HOUSES, 100, True)
                    self.assertEqual(capture['status'], 'complete')  # Stream integrity is distinct.
                    self.assertEqual(capture['attribution'], 'unknown')
                    self.assertEqual(capture['unknown_attribution'], unknown)
                    sample = dict(house='0', cycle='100')
                    audit['add_air_kill_samples']([sample], capture)
                    self.assertEqual(sample['enemy_ornithopter_kills'], 1)
                    self.assertEqual(sample['trooper_enemy_ornithopter_kills'], '')
                    self.assertEqual(sample['air_kill_attribution_complete'], 0)

    def test_complete_stream_with_no_air_callbacks_establishes_exact_zero(self):
        events = [event for event in callbacks() if event['event'] != 'lethal_damage']
        for seq, event in enumerate(events, 1):
            event['seq'] = seq
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            write_capture(out, events)
            capture = audit['air_kill_capture'](out, HOUSES, 100, True)
        sample = dict(house='0', cycle='100')
        audit['add_air_kill_samples']([sample], capture)
        self.assertEqual(capture['attribution'], 'complete')
        self.assertEqual(sample['air_kill_attribution_complete'], 1)
        self.assertEqual(sample['trooper_enemy_ornithopter_kills'], 0)

    def test_missing_disabled_limited_and_short_captures_are_not_zero_kills(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            missing = audit['air_kill_capture'](out, HOUSES, 100, True)
            disabled = audit['air_kill_capture'](out, HOUSES, 100, False)
            events = callbacks()
            events.insert(-2, dict(schema_version=1, session='fixture', cycle=90, house=-1,
                                   event='capture_limit', data={}))
            for seq, event in enumerate(events, 1):
                event['seq'] = seq
            write_capture(out, events)
            limited = audit['air_kill_capture'](out, HOUSES, 100, True)
            write_capture(out, callbacks())
            short = audit['air_kill_capture'](out, HOUSES, 101, True)
        self.assertEqual([capture['status'] for capture in (missing, disabled, limited, short)],
                         ['missing', 'disabled', 'capture_limit', 'incomplete'])
        for capture in (missing, disabled, limited, short):
            sample = dict(house='0', cycle='100')
            audit['add_air_kill_samples']([sample], capture)
            self.assertEqual(sample['air_kills_complete'], 0)
            self.assertEqual(sample['air_kill_attribution_complete'], 0)
            self.assertEqual(sample['air_kill_unknown_attribution'], '')
            for field in audit['AIR_COUNT_FIELDS']:
                self.assertEqual(sample[field], '')
        self.assertTrue(all(event['capture_complete'] == 0 for event in limited['events']))

    def test_malformed_missing_attribution_duplicate_and_gapped_callbacks_are_incomplete(self):
        variants = []
        missing = callbacks()
        del missing[1]['data']['attacker_house']
        variants.append(missing)
        duplicate = callbacks()
        duplicate[2]['data']['object'] = duplicate[1]['data']['object']
        variants.append(duplicate)
        gapped = callbacks()
        gapped[2]['seq'] += 20
        variants.append(gapped)
        unknown = callbacks()
        unknown[1]['data']['attacker_house'] = 9
        variants.append(unknown)
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            for events in variants:
                with self.subTest(events=events):
                    write_capture(out, events)
                    self.assertEqual(audit['air_kill_capture'](out, HOUSES, 100, True)['status'], 'incomplete')
            write_capture(out, callbacks())
            path = out / 'profile/ai-decisions/fixture/events.jsonl'
            with path.open('a') as handle:
                handle.write('{broken\n')
            self.assertEqual(audit['air_kill_capture'](out, HOUSES, 100, True)['status'], 'incomplete')

    def test_csv_fields_retain_product_queues_and_expanded_single_counts_separately(self):
        data = audit['parse']('DUNECITY_TROOPERS: minute=10 cycle=37500 house=0 wor=1 wor_built=2 '
                              'trooper_live=5 trooper_queued=9 trooper_single_queued=1 trooper_squads_queued=1 '
                              'trooper_squads5_queued=1 trooper_built=8 ornithopter_lost=2 elite_ornithopter_lost=3 '
                              'ornithopter_killed=7 elite_ornithopter_killed=4', 'DUNECITY_TROOPERS:')
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'samples.csv'
            audit['append'](path, audit['SAMPLE_FIELDS'], [data])
            row = audit['read'](path)[0]
        self.assertEqual(row['trooper_queued'], '9')
        self.assertEqual(row['trooper_built'], '8')
        self.assertEqual(row['wor_built'], '2')
        self.assertEqual(row['enemy_ornithopter_kills'], '')
        self.assertEqual(row['trooper_enemy_ornithopter_kills'], '')
        for field in audit['TROOPER_FIELDS'] + audit['AIR_COUNT_FIELDS'] + audit['AIR_ATTRIBUTION_FIELDS']:
            self.assertIn(field, audit['HOUSE_FIELDS'])
            self.assertIn(field, audit['SAMPLE_FIELDS'])

    def test_resume_rejects_old_csv_schema_before_scheduling_or_games(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            audit['append'](out / 'games.csv', ('id', 'error'), [dict(id='old', error='')])
            before = (out / 'games.csv').read_text()
            with self.assertRaisesRegex(ValueError, 'new results directory'):
                audit['run'](types.SimpleNamespace(results=out))
            self.assertEqual((out / 'games.csv').read_text(), before)
            with self.assertRaisesRegex(ValueError, 'CSV schema changed'):
                audit['append'](out / 'games.csv', audit['GAME_FIELDS'], [])

    def test_pairing_requires_same_source_map_options_and_off_on_styles(self):
        key = ('matrix/fixture', '0')
        old = {key: house_row()}
        new = {key: house_row()}
        previous = {'matrix/fixture': game_row('trooper-support-off')}
        current = {'matrix/fixture': game_row('trooper-support-on')}
        self.assertEqual(audit['trooper_pair_keys'](previous, current, old, new), ([key], {}))
        other_probe = {'matrix/fixture': dict(current['matrix/fixture'], probe_sha256='other-native-probe')}
        self.assertEqual(audit['trooper_pair_keys'](previous, other_probe, old, new), ([key], {}))
        missing_probe = {'matrix/fixture': dict(current['matrix/fixture'], probe_sha256='')}
        self.assertEqual(audit['trooper_pair_keys'](previous, missing_probe, old, new)[0], [])
        missing_probe = {'matrix/fixture': dict(previous['matrix/fixture'], probe_sha256='')}
        self.assertEqual(audit['trooper_pair_keys'](missing_probe, current, old, new)[0], [])
        for field in ('source_commit', 'source_fingerprint', 'map_hash', 'options_hash',
                      'content_hash', 'object_data_hash', 'sides', 'style'):
            for value in ('', 'different'):
                with self.subTest(field=field, value=value):
                    changed = {'matrix/fixture': dict(current['matrix/fixture'], **{field: value})}
                    self.assertEqual(audit['trooper_pair_keys'](previous, changed, old, new)[0], [])
        changed = {key: dict(new[key], team='9')}
        self.assertEqual(audit['trooper_pair_keys'](previous, current, old, changed)[0], [])

    def test_guardrails_require_measured_exposure_and_complete_dispatches(self):
        key = ('matrix/fixture', '0')
        old = {key: house_row()}
        new = {key: house_row(attack_count='4', kill_value='1100')}
        self.assertEqual(audit['paired_detector']([key], old, new, 'economy_stall')[1], 'no supported regression')
        self.assertEqual(audit['paired_metric']([key], old, new, 'attack_count', 1, True)[1], 'no supported regression')
        new[key]['economy_stall'] = '1'
        self.assertEqual(audit['paired_detector']([key], old, new, 'economy_stall')[1], 'inconclusive')
        new[key]['eligible_economy_stall'] = '0'
        self.assertEqual(audit['paired_detector']([key], old, new, 'economy_stall')[1], 'unmeasured')
        new[key]['air_kill_telemetry'] = 'capture_limit'
        self.assertEqual(audit['paired_metric']([key], old, new, 'attack_count', 1, True),
                         ('unmeasured', 'unmeasured'))
        del new[key]['enemy_ornithopter_kills']
        self.assertEqual(audit['paired_metric']([key], old, new, 'enemy_ornithopter_kills'),
                         ('unmeasured', 'unmeasured'))
        new[key]['air_kill_telemetry'] = 'complete'
        new[key]['air_kill_attribution_complete'] = '0'
        self.assertEqual(audit['paired_metric']([key], old, new, 'trooper_enemy_ornithopter_kills'),
                         ('unmeasured', 'unmeasured'))

    def test_sign_guardrail_clusters_houses_and_does_not_fail_tiny_point_decreases(self):
        old = {('one-game', str(house)): house_row(kill_value='1000') for house in range(6)}
        new = {key: dict(row, kill_value='999') for key, row in old.items()}
        evidence, state = audit['paired_metric'](list(old), old, new, 'kill_value', direction=1)
        self.assertEqual(state, 'inconclusive')
        self.assertIn('1 games', evidence)
        self.assertIn('p2=1.0000', evidence)  # Six houses are still only one independent sign.
        self.assertIn('point estimate decrease', evidence)
        descriptive, state = audit['paired_metric'](list(old), old, new, 'kill_value', interval=True)
        self.assertIn('game-mean delta -1.000, 95% [-1.000, -1.000]; 1 games', descriptive)
        self.assertEqual(state, 'unmeasured')  # Interval is descriptive, not a price/timing acceptance verdict.
        old = {(f'game-{game}', '0'): house_row(kill_value='1000') for game in range(6)}
        new = {key: dict(row, kill_value='999', economy_stall='1') for key, row in old.items()}
        evidence, state = audit['paired_metric'](list(old), old, new, 'kill_value', direction=1)
        self.assertEqual(state, 'regression supported')
        self.assertIn('6 games', evidence)
        self.assertIn('p2=0.0312', evidence)
        self.assertEqual(audit['paired_detector'](list(old), old, new, 'economy_stall')[1], 'regression supported')

    def test_support_cells_report_completed_histories_totals_and_missing_denominators(self):
        rows = [house_row(wor_built='0', trooper_built='3'), house_row(wor_built='2', trooper_built='7'),
                house_row(wor_built='', trooper_built='', trooper_enemy_ornithopter_kills='',
                          air_kill_attribution_complete='0')]
        self.assertIn('built>0 1/2 (50.0%)', audit['support_cell'](rows, 'wor_built'))
        self.assertIn('total 2', audit['support_cell'](rows, 'wor_built'))
        self.assertIn('total 10', audit['support_cell'](rows, 'trooper_built'))
        self.assertIn('(2/3)', audit['support_cell'](rows, 'trooper_enemy_ornithopter_kills'))

    def test_paired_report_has_hard_brutal_guardrails_and_aa_tables(self):
        with tempfile.TemporaryDirectory() as temporary:
            control, candidate = Path(temporary) / 'off', Path(temporary) / 'on'
            control.mkdir()
            candidate.mkdir()
            for directory, style in ((control, 'trooper-support-off'), (candidate, 'trooper-support-on')):
                audit['append'](directory / 'games.csv', audit['GAME_FIELDS'], [game_row(style)])
                audit['append'](directory / 'houses.csv', audit['HOUSE_FIELDS'], [
                    house_row(wor_built='1' if style.endswith('-on') else '0',
                              trooper_built='6' if style.endswith('-on') else '0',
                              trooper_enemy_ornithopter_kills='2' if style.endswith('-on') else '0'),
                    house_row(house='1', difficulty='brutal')])
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                audit['trooper_comparison'](control, candidate)
            text = output.getvalue()
            self.assertIn('2 source-matched house pairs', text)
            for label in ('| hard | all | 1 |', '| brutal | all | 1 |', 'economy_stall',
                          'bankrupt_queue_empty', 'missing_starport', 'Dispatches mean', 'Kill-value mean',
                          'WOR built', 'Trooper queued', 'Enemy Ornithopter kills', 'Own elite losses',
                          'Trooper enemy Ornithopter kills', 'built>0 0/1 (0.0%) -> 1/1 (100.0%)',
                          'totals 0 -> 6', 'totals 0 -> 2', 'two-sided paired game sign'):
                self.assertIn(label, text)
            self.assertIn('no supported regression', text)
            for directory in (control, candidate):
                rows = audit['read'](directory / 'houses.csv')
                for row in rows:
                    for field in audit['TROOPER_FIELDS'] + audit['AIR_COUNT_FIELDS']:
                        row[field] = ''
                (directory / 'houses.csv').unlink()
                audit['append'](directory / 'houses.csv', audit['HOUSE_FIELDS'], rows)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                audit['trooper_comparison'](control, candidate)
                audit['trooper_report']([control])
            self.assertIn('| hard | all | unmeasured | unmeasured |', output.getvalue())


if __name__ == '__main__':
    unittest.main()
