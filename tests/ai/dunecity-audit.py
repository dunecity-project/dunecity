#!/usr/bin/env python3
"""Reproducible DuneCity/classic QuantBot Random Skirmish audit (docs/quantbot-dunecity-audit.md).

The ordinary Random Skirmish generator and real all-QuantBot game loop supply every
measurement. Detectors identify suspicious house histories, not automatic proof of a bug.
Use dunecity-queue submit for games; --report and --compare only read captured CSVs.
Additive tech-*.csv/composition.csv sidecars retain replacement-price composition,
accepted commitments, exact first-owned/completed technology and four-minute
quote-budget opportunities. Raw engine outcomes and placement scores remain alongside
the audit-only DuneCity population adjudication.

Fleet metrics are pure per-tick observations, not AI estimates. samples.csv and
houses.csv retain gross refined-spice credits (also exact Q32 refined_spice_raw),
asset replacement capital, original-house harvester losses and unload-start to
unload-start trip totals. Open/left/interrupted/unstarted exposures are separate
from completed trips; one cycle is 16 ms. Bay occupancy includes empty harvesters
waiting for pickup; unloading requires positive cargo. Tick integrals use the
previous observed state and exclude intervals spanning observation gaps.
For a gap-free window, spice credits per minute per replacement-capital credit
is delta(refined_spice_raw) / 2**32 * 3750 / delta(fleet_capital_value_cycles).
Capital built value is the engine's built ledger at current replacement prices,
not actual expenditure (free deliveries and initial assets need not be purchases).
refinery-samples.csv retains each refinery's eight-neighbour unweighted ground
distance from its footprint to reachable spice, contiguous spice-patch amount,
nearest patch's ground-component amount and union of perimeter-accessible spice.
The same per-refinery records are embedded as fleet_refinery_metrics JSON in
samples.csv/houses.csv, so collectors retaining only the main tables lose no fields.
Geometry ignores fog/transient units, blocks mountains/structures, and runs no
more than once per 3750 cycles. geometry_cycle identifies stale final snapshots;
new refineries without a geometry snapshot and unreachable distances are blank.
Component IDs are row-major root tile indices within that snapshot, not enduring
field identities. Overlapping refinery supplies must not be summed. Distance is
not a travel-time model: terrain speed, turns, traffic and carryalls are excluded.
"""
import base64
import argparse
import configparser
import csv
import hashlib
import json
import math
import os
import random
import shutil
import statistics
import subprocess
import sys
import threading
import time
from collections import defaultdict
from bisect import bisect_right
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/performance'))
sys.path.insert(0, str(root / 'tests/ai'))
from probe_profile import finish_game, headless_asset_arguments, probe_binary, remove_tree, require_free_space, scratch_dir  # noqa: E402
import match_score  # noqa: E402
from ai_map_features import MAP_FEATURE_FIELDS, rows_from_log  # noqa: E402
from fleet_observer import FLEET_VALUES, REFINERY_VALUES, fleet_observations  # noqa: E402

HOUSES = ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary')
HOUSE_IDS = {'harkonnen': 0, 'atreides': 1, 'ordos': 2, 'fremen': 3, 'sardaukar': 4, 'mercenary': 5}
CLASSES = {'easy': 'qBotEasy', 'medium': 'qBotMedium', 'hard': 'qBotHard', 'brutal': 'qBotBrutal'}
SPICE = {'none': 0, 'low': 1, 'medium': 2, 'high': 3}
DETECTORS = ('economy_stall', 'power_deficit', 'no_ground_defence', 'missing_starport', 'missing_radar',
             'missing_heavy_factory', 'missing_ix', 'idle_under_attack', 'unused_credits',
             'bankrupt_queue_empty', 'early_no_fight', 'zero_harvesters', 'storage_full')
META = ('id', 'seed', 'size', 'spice_level', 'format', 'difficulty', 'variant', 'base', 'army', 'city', 'limit', 'style', 'initial_tech')
TROOPER_FIELDS = ('wor', 'wor_built', 'trooper_live', 'trooper_queued', 'trooper_single_queued',
                  'trooper_squads_queued', 'trooper_squads5_queued', 'trooper_built', 'ornithopter_live',
                  'elite_ornithopter_live', 'ornithopter_built', 'elite_ornithopter_built', 'ornithopter_lost',
                  'elite_ornithopter_lost', 'ornithopter_killed', 'elite_ornithopter_killed')
# Stable engine item IDs (include/data.h); lethal_damage names the victim type.
ENEMY_AIR_FIELDS = {35: 'enemy_ornithopter_kills', 80: 'enemy_elite_ornithopter_kills'}
TROOPER_ITEM = 44
TROOPER_ENEMY_AIR_FIELDS = {item: 'trooper_' + field for item, field in ENEMY_AIR_FIELDS.items()}
AIR_COUNT_FIELDS = tuple(ENEMY_AIR_FIELDS.values()) + tuple(TROOPER_ENEMY_AIR_FIELDS.values())
AIR_ATTRIBUTION_FIELDS = ('air_kill_attribution_complete', 'air_kill_unknown_attribution')
REFINERY_FIELDS = META + REFINERY_VALUES
GAME_FIELDS = META + ('sides', 'map_seed', 'terrain_attempts', 'skipped', 'outcome', 'winner_team', 'minutes',
                      'loop_ms', 'wall_s', 'digest', 'attack_telemetry', 'error', 'config_hash', 'options_hash',
                      'map_hash', 'content_hash', 'object_data_hash', 'ai_config_hashes', 'probe_sha256',
                      'source_commit', 'source_fingerprint', 'source_fingerprint_error',
                      'air_kill_telemetry', 'air_kill_telemetry_reasons', 'air_kill_attribution',
                      'air_kill_unknown_attribution', 'capture_mib', 'diagnostics',
                      'mod', 'candidate_house', 'candidate_seat', 'candidate_style', 'sample_case', 'sample_seed', 'research_enabled',
                      'scoring_rule', 'scored_winner_team', 'win_condition', 'team_populations',
                      'seat_styles', 'style_cohort', 'style_rotation', 'observer_mode',
                      'fleet_observer_status', 'fleet_house_samples', 'fleet_refinery_samples')
HOUSE_FIELDS = META + ('house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                       'army_value', 'army_units', 'sortie_minutes', 'attack_telemetry', 'attack_count',
                       'first_attack_minute', 'storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost',
                       'score', 'drawn', 'won', 'flags') + tuple(
    field for detector in DETECTORS for field in (detector, 'eligible_' + detector, 'first_' + detector)
) + TROOPER_FIELDS + ('air_kill_telemetry',) + AIR_ATTRIBUTION_FIELDS + AIR_COUNT_FIELDS + (
    'population', 'raw_score', 'raw_drawn', 'raw_won', 'seat_style', 'harvesters', 'refineries') + FLEET_VALUES
CYCLES_PER_MINUTE = 3750  # The engine's 16 ms fixed simulation tick.
SAMPLE_VALUES = ('minute', 'cycle', 'house', 'team', 'alive', 'credits', 'army_value', 'army_units', 'harvesters',
                 'structures', 'factories', 'kill_value', 'loss_value', 'power', 'need', 'population', 'tax', 'demand',
                 'built', 'structures_lost', 'base_hp', 'yards', 'yard_level', 'queue', 'upgrading', 'waiting', 'fronts',
                 'gun', 'rocket', 'radar', 'radar_built', 'starport', 'starport_built', 'heavy', 'heavy_built', 'ix',
                 'ix_built', 'fighters', 'moving', 'targeting', 'enemies', 'enemy_value', 'covered_enemies', 'spice',
                 'ledger_enabled', 'spice_gross', 'tax_gross', 'starting_cash', 'spice_cash', 'city_cash', 'capacity',
                 'yard_storage', 'police_funding', 'army_target', 'unit_limit', 'spice_multiplier', 'sale_credits',
                 'spent_total', 'power_spent', 'police_charged', 'refunded', 'storage_lost', 'city_net_applied',
                 'storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost'
) + FLEET_VALUES + TROOPER_FIELDS + ('air_kills_complete',) + AIR_ATTRIBUTION_FIELDS + AIR_COUNT_FIELDS
BOOM_FIELDS = ('boom_phase', 'boom_stop_minute', 'boom_own_army', 'boom_enemy_army', 'boom_near_army',
               'boom_own_economy', 'boom_leader_economy', 'boom_army_ratio_bps', 'boom_economy_ratio_bps')
SAMPLE_FIELDS = META + SAMPLE_VALUES + ('flags', 'refineries', 'power_required') + BOOM_FIELDS
ATTACK_FIELDS = META + ('house', 'cycle', 'members', 'value')
AIR_KILL_FIELDS = META + ('session', 'seq', 'cycle', 'house', 'attacker', 'victim_house', 'object', 'item',
                          'enemy', 'capture_complete', 'attacker_item', 'attacker_source', 'attacker_firing_house',
                          'attacker_attribution', 'source_enemy', 'attribution_complete')
# Additive sidecars leave the original CSV contracts and historical readers intact.
# Shares use only the ten unit_mix vehicle slots; infantry has a separate quota.
MIX_SLOTS = ('tank', 'siege', 'launcher', 'devastator', 'sonic_tank', 'deviator',
             'ornithopter', 'trike', 'raider_trike', 'quad', 'infantry', 'other')
TECH_ITEMS = {4: 'heavy', 6: 'ix', 16: 'starport', 8: 'palace', 11: 'repair', 5: 'hitech'}
PRODUCTION_ITEMS = {1, 4, 5, 6, 7, 8, 9, 11, 16, 19, 57, 60, 65, 72, 88}
ECONOMY_ITEMS = {2, 10, 13, 14, 15, 17, 18, 20, 21, 22, 23, 24, 25, 26,
                 27, 31, 34, 48, 49, 56, 58, 59, 61, 66, 76, 85, 86}
MILITARY_ITEMS = {3, 12, 28, 29, 32, 33, 35, 36, 37, 39, 40, 41, 42, 43, 44,
                  45, 46, 47, 52, 53, 54, 55, 62, 63, 64, 71, 74, 75, 77, 78, 79, 80, 81, 82, 83, 84, 87}
CHEAP_ITEMS = {32, 36, 42, 43, 44, 46, 47, 52, 63, 74, 75, 77, 78, 79, 81}
TECH_SPEND_ITEMS = set(TECH_ITEMS) | {9, 88}
CITY_SPEND_ITEMS = ECONOMY_ITEMS - {27, 31, 34, 59, 66, 85, 86}
SPEND_CATEGORIES = ('production', 'economy', 'military', 'other', 'cheap_unit', 'tech', 'city')
SPEND_FLAGS = {'cheap_unit': 'cheap_unit', 'tech': 'tech', 'city': 'city_spend'}
TECH_HORIZON = 4 * CYCLES_PER_MINUTE
TECH_SAMPLE_VALUES = ('minute', 'cycle', 'house', 'credits', 'military_live_count', 'military_live_value',
                      'military_queued_count', 'military_queued_value', 'military_committed_count',
                      'military_committed_value', 'mean_military_unit_cost', 'mean_committed_unit_cost',
                      'allocation_ready', 'allocation_cycle', 'infantry_quota_bps', 'tech_level',
                      'research_target', 'research_remaining', 'research_enabled', 'city_sim',
                      'structures', 'army_value', 'army_units', 'spice_gross', 'tax_gross', 'spent_total',
                      'bucket_initialized', 'bucket_last_wealth', 'bucket_production_credits_x10000',
                      'bucket_economy_credits_x10000', 'bucket_military_credits_x10000',
                      'order_capture', 'spending_complete') + tuple('accepted_' + name for name in SPEND_CATEGORIES)
TECH_SAMPLE_FIELDS = META + ('mod',) + TECH_SAMPLE_VALUES
COMPOSITION_FIELDS = META + ('mod', 'minute', 'cycle', 'house', 'slot', 'item', 'live_count', 'live_value',
                             'queued_count', 'queued_value', 'committed_count', 'committed_value',
                             'live_count_bps', 'live_value_bps', 'committed_count_bps', 'committed_value_bps',
                             'target_bps', 'value_target_delta_bps', 'allocation_ready', 'allocation_cycle')
TECH_ORDER_FIELDS = META + ('mod', 'session', 'seq', 'cycle', 'house', 'event', 'item', 'builder',
                            'builder_item', 'price', 'bucket', 'cheap_unit', 'tech', 'city_spend', 'capture_complete',
                            'rule', 'composition_basis', 'composition_before', 'composition_after',
                            'state_id', 'state_cycle', 'military_value', 'military_cap', 'military_budget', 'military_headroom')
TECH_PLAN_CONTEXT = ('tech_savings', 'savings_policy', 'income_buckets', 'tech_funding',
                     'active', 'stable', 'spendable', 'working_reserve', 'defence_minimum',
                     'ready_ground_units', 'ready_ground_value', 'production_bps', 'economy_bps', 'military_bps',
                     'ledger_initialized', 'production_balance', 'economy_balance', 'military_balance')
TECH_PLAN_FIELDS = META + ('mod', 'session', 'seq', 'cycle', 'house', 'feasible', 'candidate_kind', 'item',
                           'builder', 'cost', 'cash', 'committed_cost', 'forecast_net_income', 'horizon_cycles'
                           ) + TECH_PLAN_CONTEXT + ('fully_observed_4m', 'affordable_without_cheap_4m',
                           'accepted_cheap_4m', 'candidate_ordered_4m', 'affordable_after_cheap_quotes_4m',
                           'quoted_diversion_4m')
TECH_HOUSE_VALUES = ('order_capture', 'spending_complete', 'mean_military_unit_cost', 'mean_committed_unit_cost',
                     'first_upgrade_order_minute', 'first_upgrade_completed_observed_minute',
                     'first_research_order_minute', 'first_research_completed_minute',
                     'feasible_plans', 'observed_4m_plans', 'affordable_4m_plans', 'quoted_diversion_4m_plans'
                     ) + tuple('accepted_' + name for name in SPEND_CATEGORIES) + tuple(
    f'first_{kind}_{name}_minute' for name in TECH_ITEMS.values() for kind in ('owned', 'completed', 'order'))
TECH_HOUSE_FIELDS = META + ('mod', 'house') + TECH_HOUSE_VALUES
CHEAP_PATH_FIELDS = META + ('mod', 'house', 'rule', 'composition_basis', 'item', 'capture_complete',
                            'order_count', 'accepted_credits', 'observed_orders', 'observed_priced_credits', 'missing_prices')
TECH_TABLES = (('tech-samples.csv', TECH_SAMPLE_FIELDS), ('composition.csv', COMPOSITION_FIELDS),
               ('tech-orders.csv', TECH_ORDER_FIELDS), ('tech-opportunities.csv', TECH_PLAN_FIELDS),
               ('tech-houses.csv', TECH_HOUSE_FIELDS), ('cheap-paths.csv', CHEAP_PATH_FIELDS))
lock = threading.Lock()
SAMPLE_FACTORS = {
    'format': ('1v1', '2v2', 'ffa4', 'ffa6'),
    'difficulty': ('easy', 'medium', 'hard', 'brutal'),
    'size': (96, 128, 160),
}
SAMPLE_STRATA = ('format', 'difficulty', 'size', 'spice_level', 'initial_tech', 'candidate_seat')


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def require_schema(path, fields):
    if path.exists():
        with path.open(newline='') as handle:
            if next(csv.reader(handle), []) != list(fields):
                raise ValueError(f'CSV schema changed; use a new results directory: {path}')


def append(path, fields, rows):
    with lock:
        new = not path.exists()
        require_schema(path, fields)
        with path.open('a', newline='') as handle:
            writer = csv.DictWriter(handle, fieldnames=fields, extrasaction='ignore')
            if new:
                writer.writeheader()
            writer.writerows(rows)


def parse(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)




def air_kill_capture(out, houses, final_cycle, enabled):
    """Retain actual lethal callbacks and distinguish stream from source completeness.

    Existing enemy_* counts follow lethal damagerOwner, including a dead shooter's
    shot. Trooper counts instead require the natural source item and firing house
    from the shared damage provenance (never its possibly-Deviator reward bucket).
    Unknown source attribution is retained, not guessed from a live object/army.
    One unknown air callback conservatively censors game-wide exact Trooper totals.
    """
    if not enabled:
        return dict(status='disabled', reasons='', attribution='disabled', unknown_attribution='', events=[], decisions=[])
    paths = sorted((out / 'profile').rglob('events.jsonl'))
    teams = {int(house['house']): int(house['team']) for house in houses}
    reasons, events, decisions = [], [], []
    limited = False
    if len(paths) != 1:
        reasons.append('missing_capture' if not paths else 'multiple_captures')
    for path in paths:
        started = ended = summaries = last_seq = 0
        end_cycle, session = None, None
        summary_houses, victims = set(), set()
        states = {}
        with path.open(encoding='utf-8', errors='replace') as handle:
            for line in handle:
                try:
                    event = json.loads(line)
                    if not isinstance(event, dict) or not isinstance(event.get('data'), dict):
                        raise ValueError('not a telemetry record')
                    seq, cycle = int(event['seq']), int(event['cycle'])
                    if seq != last_seq + 1:
                        reasons.append('sequence_gaps')
                    last_seq = seq
                    if session is None:
                        session = event['session']
                    if not session or event['session'] != session or event.get('schema_version') != 1:
                        reasons.append('invalid_session')
                    name, data = event['event'], event['data']
                    if name == 'session_start':
                        started += 1
                    elif name == 'session_end':
                        ended += 1
                        end_cycle = cycle
                    elif name == 'game_summary':
                        summaries += 1
                        summary_houses.update(int(house) for house in data['houses'])
                    elif name == 'capture_limit':
                        limited = True
                    elif name == 'simulation_exception':
                        reasons.append('simulation_exception')
                    elif name == 'state_snapshot':
                        states[seq] = (cycle, data.get('state', {}))
                    elif name in ('production_order', 'capital_upgrade', 'city_yard_upgrade', 'capital_road_batch',
                                   'tech_research_started', 'tech_research_completed', 'tech_savings_plan',
                                   'construction_completed'):
                        if 'state' not in data and data.get('state_id') in states:
                            state_cycle, state = states[data['state_id']]
                            data = dict(data, state=state, state_cycle=state_cycle)
                        decisions.append(dict(session=session, seq=seq, cycle=cycle, house=int(event['house']),
                                              event=name, data=data))
                    elif name == 'lethal_damage' and int(data['item']) in ENEMY_AIR_FIELDS:
                        victim, attacker_house = int(event['house']), int(data['attacker_house'])
                        object_id, attacker = int(data['object']), int(data['attacker'])
                        if object_id in victims:
                            reasons.append('duplicate_air_death')
                            continue
                        victims.add(object_id)
                        enemy = ''
                        if victim not in teams or (attacker_house >= 0 and attacker_house not in teams):
                            reasons.append('unknown_combat_house')
                        elif attacker_house >= 0:
                            enemy = int(teams[victim] != teams[attacker_house])
                        source_item = int(data.get('attacker_item', -1))
                        source = int(data.get('attacker_source', -1))
                        firing_house = int(data.get('attacker_firing_house', -1))
                        attribution = data.get('attacker_attribution', 'unknown')
                        source_enemy = int(teams[victim] != teams[firing_house]) if (
                            victim in teams and firing_house in teams) else ''
                        attributed = source_item >= 0 and source == attacker and firing_house in teams \
                            and attribution in ('snapshot', 'direct')
                        events.append(dict(session=session, seq=seq, cycle=cycle, house=attacker_house,
                                           attacker=attacker, victim_house=victim, object=object_id,
                                           item=int(data['item']), enemy=enemy, attacker_item=source_item,
                                           attacker_source=source, attacker_firing_house=firing_house,
                                           attacker_attribution=attribution, source_enemy=source_enemy,
                                           attribution_complete=int(attributed)))
                except (ValueError, TypeError, KeyError):
                    reasons.append('malformed_records')
        if started != 1 or ended != 1 or summaries != 1:
            reasons.append('missing_session_boundary_or_summary')
        if end_cycle is None or end_cycle < final_cycle:
            reasons.append('short_capture')
        if set(teams) - summary_houses:
            reasons.append('incomplete_final_roster')
    if limited:
        reasons.append('capture_limit')
    complete = not reasons
    for event in events:
        event['capture_complete'] = int(complete)
    status = 'complete' if complete else 'capture_limit' if limited else 'missing' if not paths else 'incomplete'
    unknown = sum(not event['attribution_complete'] for event in events)
    return dict(status=status, reasons=','.join(dict.fromkeys(reasons)), events=events, decisions=decisions,
                attribution=('unknown' if unknown else 'complete') if complete else status,
                unknown_attribution=unknown if paths else '')


def add_air_kill_samples(samples, capture):
    cycles, unknown = defaultdict(list), []
    for event in capture['events']:
        if event['enemy'] == 1:
            cycles[(event['house'], ENEMY_AIR_FIELDS[event['item']])].append(event['cycle'])
        if not event['attribution_complete']:
            unknown.append(event['cycle'])
        elif event['source_enemy'] == 1 and event['attacker_item'] == TROOPER_ITEM:
            cycles[(event['attacker_firing_house'], TROOPER_ENEMY_AIR_FIELDS[event['item']])].append(event['cycle'])
    unknown.sort()
    for values in cycles.values():
        values.sort()
    complete = capture['status'] == 'complete'
    for sample in samples:
        cycle, house = int(sample['cycle']), int(sample['house'])
        missing = bisect_right(unknown, cycle)
        source_complete = complete and missing == 0
        sample.update(air_kills_complete=int(complete), air_kill_attribution_complete=int(source_complete),
                      air_kill_unknown_attribution=missing if complete else '')
        for field in AIR_COUNT_FIELDS:
            measured = source_complete if field in TROOPER_ENEMY_AIR_FIELDS.values() else complete
            sample[field] = bisect_right(cycles[(house, field)], cycle) if measured else ''


def metadata(game):
    return {field: game[field] for field in META}


def accepted_order(event):
    """Full accepted commitments, not installment payments or completed products."""
    data, name = event['data'], event['event']
    if name == 'production_order':
        if not int(data.get('accepted', 0)):
            return None
        item = int(data['item'])
        quantity = int(data.get('queue_after', 1)) - int(data.get('queue_before', 0))
        price = int(data['quoted_price']) * quantity if 'quoted_price' in data and quantity > 0 else ''
        bucket = data.get('spending_bucket') or ('production' if item in PRODUCTION_ITEMS else
                 'economy' if item in ECONOMY_ITEMS else 'military' if item in MILITARY_ITEMS else 'other')
        tech = item in TECH_SPEND_ITEMS or item in (14, 15) and bucket == 'production'
        cheap, city = item in CHEAP_ITEMS, item in CITY_SPEND_ITEMS and bucket == 'economy'
    elif name in ('capital_upgrade', 'city_yard_upgrade', 'tech_research_started'):
        if name != 'tech_research_started' and not int(data.get('accepted', 0)):
            return None
        item = data.get('item', '')
        key = 'price' if name == 'capital_upgrade' else 'cost'
        price = int(data[key]) if key in data else ''
        bucket, tech, cheap, city = 'production', True, False, False
    elif name == 'capital_road_batch':
        item, price = 23, int(data['cost']) if 'cost' in data else ''
        bucket, tech, cheap, city = 'economy', False, False, True
    else:
        return None
    if bucket not in SPEND_CATEGORIES[:4]:
        bucket = 'other'
    state = data.get('state', {})
    military, budget = state.get('military', ''), state.get('military_budget', state.get('military_limit', ''))
    context = dict(rule=data.get('rule', ''), composition_basis=data.get('composition_basis', ''),
                   composition_before=json.dumps(data['composition_before'], separators=(',', ':'), sort_keys=True)
                       if 'composition_before' in data else '',
                   composition_after=json.dumps(data['composition'], separators=(',', ':'), sort_keys=True)
                       if 'composition' in data else '',
                   state_id=data.get('state_id', ''), state_cycle=data.get('state_cycle', event['cycle']) if state else '',
                   military_value=military, military_cap=state.get('military_limit', ''), military_budget=budget,
                   military_headroom=int(budget)-int(military) if military != '' and budget != '' else '')
    return {key: event[key] for key in ('session', 'seq', 'cycle', 'house', 'event')} | dict(
        item=item, builder=data.get('builder', data.get('lab', '')), builder_item=data.get('builder_item', ''),
        price=price, bucket=bucket, cheap_unit=int(cheap), tech=int(tech), city_spend=int(city), **context)


def cheap_path_rows(orders, complete):
    groups = defaultdict(list)
    for order in orders:
        if order['cheap_unit']:
            groups[(order['house'], order['rule'], order['composition_basis'], order['item'])].append(order)
    rows = []
    for (house, rule, basis, item), group in groups.items():
        missing = sum(order['price'] == '' for order in group)
        total = sum(order['price'] for order in group if order['price'] != '')
        rows.append(dict(house=house, rule=rule, composition_basis=basis, item=item,
                         capture_complete=int(complete), order_count=len(group) if complete else '',
                         accepted_credits=total if complete and not missing else '',
                         observed_orders=len(group), observed_priced_credits=total, missing_prices=missing))
    return rows


def composition_rows(lines):
    groups = defaultdict(dict)
    for line in lines:
        if 'DUNECITY_COMPOSITION:' in line:
            data = parse(line, 'DUNECITY_COMPOSITION:')
            groups[(int(data['house']), int(data['cycle']))][int(data['slot'])] = data
    rows = []
    for slots in groups.values():
        # No guessed denominators when a log was truncated between slot rows.
        complete = set(slots) == set(range(len(MIX_SLOTS)))
        totals = {field: sum(int(row[field]) for slot, row in slots.items() if slot < 10)
                  for field in ('live_count', 'live_value', 'queued_count', 'queued_value')}
        for slot, data in sorted(slots.items()):
            row = dict(data, slot=MIX_SLOTS[slot])
            for suffix in ('count', 'value'):
                row['committed_' + suffix] = int(row['live_' + suffix]) + int(row['queued_' + suffix])
                for prefix in ('live', 'committed'):
                    denominator = totals['live_' + suffix] + (totals['queued_' + suffix] if prefix == 'committed' else 0)
                    row[prefix + '_' + suffix + '_bps'] = round(10000 * int(row[prefix + '_' + suffix])
                        / denominator, 3) if complete and slot < 10 and denominator else ''
            target = int(row['target_bps'])
            if target < 0:
                row['target_bps'] = ''
            share = row['committed_value_bps']
            row['value_target_delta_bps'] = round(share - target, 3) if share != '' and target >= 0 else ''
            rows.append(row)
    return rows


def tech_audit(lines, samples, final, capture):
    """Supplemental, read-only tables. Missing/limited capture is never zero spend."""
    complete = capture['status'] == 'complete'
    decisions = capture.get('decisions', [])
    primary_upgrades = {(event['house'], event['cycle'], event['data'].get('builder'))
                        for event in decisions if event['event'] == 'capital_upgrade'}
    orders = [order for event in decisions
              if not (event['event'] == 'city_yard_upgrade'
                      and (event['house'], event['cycle'], event['data'].get('builder')) in primary_upgrades)
              and (order := accepted_order(event)) is not None]
    orders.sort(key=lambda row: (row['cycle'], row['seq']))
    priced = complete and all(order['price'] != '' for order in orders)
    for order in orders:
        order['capture_complete'] = int(complete)
    by_house = defaultdict(list)
    for order in orders:
        by_house[int(order['house'])].append(order)
    totals = {int(house['house']): dict.fromkeys(SPEND_CATEGORIES, 0) for house in final}
    tech_samples = {}
    structures, builders = [], []
    for line in lines:
        for marker, destination in (('DUNECITY_TECH_STRUCTURE:', structures), ('DUNECITY_BUILDER:', builders)):
            if marker in line:
                destination.append(parse(line, marker))
        if 'DUNECITY_TECH_SAMPLE:' in line:
            data = parse(line, 'DUNECITY_TECH_SAMPLE:')
            tech_samples[(int(data['house']), int(data['cycle']))] = data
    for line in lines:
        if 'DUNECITY_SPENDING_BUCKETS:' in line:
            data = parse(line, 'DUNECITY_SPENDING_BUCKETS:')
            key = (int(data['house']), int(data['cycle']))
            if key in tech_samples:
                tech_samples[key].update(data)
    indices = defaultdict(int)
    for sample in sorted(samples, key=lambda row: int(row['cycle'])):
        house, cycle = int(sample['house']), int(sample['cycle'])
        row = tech_samples.get((house, cycle))
        if row is None:
            continue
        while indices[house] < len(by_house[house]) and by_house[house][indices[house]]['cycle'] <= cycle:
            order = by_house[house][indices[house]]
            if order['price'] != '':
                totals[house][order['bucket']] += order['price']
                for category, flag in SPEND_FLAGS.items():
                    if order[flag]:
                        totals[house][category] += order['price']
            indices[house] += 1
        for field in ('structures', 'army_value', 'army_units', 'spice_gross', 'tax_gross', 'spent_total'):
            row[field] = sample.get(field, '')
        for suffix in ('count', 'value'):
            row['military_committed_' + suffix] = int(row['military_live_' + suffix]) + int(row['military_queued_' + suffix])
        for prefix, field in (('live', 'mean_military_unit_cost'), ('committed', 'mean_committed_unit_cost')):
            count = int(row['military_' + prefix + '_count'])
            row[field] = round(int(row['military_' + prefix + '_value']) / count, 3) if count else ''
        row.update(order_capture=capture['status'], spending_complete=int(priced))
        row.update({'accepted_' + name: value if priced else '' for name, value in totals[house].items()})
    final_cycle = max((int(sample['cycle']) for sample in samples), default=0)
    ends = {int(house['house']): min(final_cycle, round(float(house['eliminated_s']) * CYCLES_PER_MINUTE / 60))
            if house.get('alive') == '0' else final_cycle for house in final}
    plans = []
    for event in decisions:
        if event['event'] != 'tech_savings_plan':
            continue
        data, house, cycle = event['data'], int(event['house']), int(event['cycle'])
        row = {field: event[field] for field in ('session', 'seq', 'cycle', 'house')}
        row.update({field: data.get(field, '') for field in ('feasible', 'candidate_kind', 'item', 'builder',
                    'cost', 'cash', 'committed_cost', 'forecast_net_income', 'horizon_cycles') + TECH_PLAN_CONTEXT})
        row.update({field: '' for field in ('fully_observed_4m', 'affordable_without_cheap_4m',
                    'accepted_cheap_4m', 'candidate_ordered_4m', 'affordable_after_cheap_quotes_4m', 'quoted_diversion_4m')})
        required = ('feasible', 'cost', 'cash', 'committed_cost', 'forecast_net_income', 'horizon_cycles', 'working_reserve')
        if priced and all(row[field] != '' for field in required) and int(row['horizon_cycles']) == TECH_HORIZON:
            observed = ends.get(house, 0) >= cycle + TECH_HORIZON
            row['fully_observed_4m'] = int(observed)
            if observed and int(row['feasible']):
                window = [order for order in by_house[house]
                          if (cycle, event['seq']) < (order['cycle'], order['seq'])
                          and order['cycle'] <= cycle + TECH_HORIZON]
                cheap = sum(order['price'] for order in window if order['cheap_unit'])
                kind = row['candidate_kind']
                purchased = any((kind == 'structure' and order['event'] == 'production_order'
                                 and str(order['item']) == str(row['item']))
                                or (kind == 'upgrade' and order['event'] == 'capital_upgrade'
                                    and str(order['builder']) == str(row['builder']))
                                or (kind == 'research' and order['event'] == 'tech_research_started') for order in window)
                funding = max(0, int(row['cash']) - int(row['committed_cost']) - int(row['working_reserve'])) \
                    + max(0, int(row['forecast_net_income']))
                before, after = funding >= int(row['cost']), funding - cheap >= int(row['cost'])
                row.update(affordable_without_cheap_4m=int(before), accepted_cheap_4m=cheap,
                           candidate_ordered_4m=int(purchased), affordable_after_cheap_quotes_4m=int(after),
                           quoted_diversion_4m=int(before and cheap > 0 and not after and not purchased))
        plans.append(row)
    house_rows = []
    for house in final:
        number = int(house['house'])
        row = dict(house=number, order_capture=capture['status'], spending_complete=int(priced))
        points = [point for (owner, _), point in tech_samples.items() if owner == number]
        last = max(points, key=lambda point: int(point['cycle'])) if points else {}
        for field in ('mean_military_unit_cost', 'mean_committed_unit_cost'):
            row[field] = last.get(field, '')
        for name in SPEND_CATEGORIES:
            values = [order['price'] for order in by_house[number]
                      if order['bucket'] == name or name in SPEND_FLAGS and order[SPEND_FLAGS[name]]]
            row['accepted_' + name] = sum(values) if priced else ''
        own = [event for event in decisions if int(event['house']) == number]
        for name, event_name in (('upgrade_order', 'capital_upgrade'), ('research_order', 'tech_research_started'),
                                 ('research_completed', 'tech_research_completed')):
            cycles = [int(event['cycle']) for event in own
                      if (event['event'] == event_name and (event_name != 'capital_upgrade'
                          or int(event['data'].get('accepted', 0))))
                      or (event_name == 'capital_upgrade' and event['event'] == 'city_yard_upgrade'
                          and int(event['data'].get('accepted', 0)))]
            row['first_' + name + '_minute'] = min(cycles) / CYCLES_PER_MINUTE if cycles and complete else ''
        levels, completed = {}, []
        for builder in sorted((entry for entry in builders if int(entry['house']) == number),
                              key=lambda entry: int(entry['cycle'])):
            key, level = builder['builder'], int(builder['level'])
            if key in levels and level > levels[key]:
                completed.append(int(builder['cycle']) / CYCLES_PER_MINUTE)
            levels[key] = level
        row['first_upgrade_completed_observed_minute'] = min(completed) if completed else ''
        for entry in structures:
            if int(entry['house']) != number:
                continue
            for kind in ('owned', 'completed'):
                cycle = int(entry['first_' + kind + '_cycle'])
                if cycle >= 0:
                    row[f'first_{kind}_{entry["name"]}_minute'] = cycle / CYCLES_PER_MINUTE
        for item, name in TECH_ITEMS.items():
            cycles = [order['cycle'] for order in by_house[number]
                      if order['event'] == 'production_order' and str(order['item']) == str(item)]
            row[f'first_order_{name}_minute'] = min(cycles) / CYCLES_PER_MINUTE if cycles and complete else ''
        own_plans = [plan for plan in plans if int(plan['house']) == number]
        for field, condition in (
                ('feasible_plans', lambda plan: str(plan['feasible']) == '1'),
                ('observed_4m_plans', lambda plan: plan['fully_observed_4m'] == 1),
                ('affordable_4m_plans', lambda plan: plan['affordable_without_cheap_4m'] == 1),
                ('quoted_diversion_4m_plans', lambda plan: plan['quoted_diversion_4m'] == 1)):
            row[field] = sum(condition(plan) for plan in own_plans) if own_plans and complete else ''
        house_rows.append(row)
    return dict(zip((name for name, _ in TECH_TABLES),
                    (list(tech_samples.values()), composition_rows(lines), orders, plans, house_rows,
                     cheap_path_rows(orders, complete))))


def balanced_draw(levels, count, rng):
    """Independent shuffled factor decks: each level occurs floor/ceil(count / levels) times."""
    whole, extra = divmod(count, len(levels))
    draw = list(levels) * whole + rng.sample(list(levels), extra)
    rng.shuffle(draw)
    return draw


def game_limit(args, mod):
    """Short diagnostic runs remain available; classic games never exceed 90 minutes."""
    return min(args.minutes, 120 if mod == 'dunecity' else 90)


def manifest_sample_schedule(args):
    """Explicit preregistered map cells, shared unchanged by every screen arm."""
    path = Path(args.case_manifest)
    if not path.is_absolute():
        path = root / path
    manifest = json.loads(path.read_text())
    if manifest.get('schema_version') != 1 or not manifest.get('cases'):
        raise ValueError('Case manifest requires schema_version=1 and a nonempty cases list')
    if args.mod not in ('vanilla', 'dunecity'):
        raise ValueError('Case manifest requires one explicit content mode')
    sample_seed = int(manifest['sample_seed'])
    if not 0 <= sample_seed <= 0xffffffff:
        raise ValueError('Case manifest sample_seed must be unsigned 32-bit')
    identity = hashlib.sha256(json.dumps(manifest, sort_keys=True,
                                        separators=(',', ':')).encode()).hexdigest()[:16]
    games = []
    minutes = game_limit(args, args.mod)
    for index, cell in enumerate(manifest['cases']):
        fmt, size = cell['format'], int(cell['size'])
        spice, tech, seed = cell['spice_level'], int(cell['initial_tech']), int(cell['seed'])
        enemy = cell.get('difficulty', 'brutal')
        rock, mountain = int(cell.get('rock', 30)), int(cell.get('mountain', 5))
        # Random Skirmish map sizes and 5% terrain steps: scenario-match rejects
        # any recipe the generator would clamp, so fail before queueing arms.
        if (fmt not in ('1v1', '2v2', 'ffa4', 'ffa6') or size not in (64, 96, 128, 160, 192, 256)
                or spice not in SPICE or spice == 'none' or not 1 <= tech <= 9
                or not 0 <= seed <= 0xffffffff or enemy not in CLASSES
                or rock % 5 or mountain % 5 or not 5 <= rock <= 90 or not 0 <= mountain <= 50
                or rock + mountain >= 100):
            raise ValueError(f'Unsupported case-manifest cell {index + 1}')
        count = 2 if fmt == '1v1' else 6 if fmt == 'ffa6' else 4
        houses = list(HOUSES[:count])
        shift = index % count
        houses = houses[shift:] + houses[:shift]
        teams = [1, 1, 2, 2] if fmt == '2v2' else list(range(1, count + 1))
        spec = (f'size={size},base={args.base},army={args.army},city={args.city},'
                f'spice={SPICE[spice]},sand={100-rock-mountain},rock={rock},mountain={mountain},'
                f'tech={tech},credits=3000,seed={seed}')
        prefix = (f'sample/{args.mod}/design-{identity}/s{sample_seed}-n{len(manifest["cases"])}/'
                  f'{args.base}-a{args.army}-c{args.city}/{index + 1:03d}/{size}/{spice}/'
                  f'{fmt}/{enemy}/t{tech}/{minutes}m/{seed}')
        for swapped, seat in enumerate((0, 2 if fmt == '2v2' else count - 1)):
            sides = [(house, team, 'brutal' if i == seat else enemy)
                     for i, (house, team) in enumerate(zip(houses, teams))]
            games.append(dict(id=f'{prefix}/{"swap" if swapped else "original"}',
                              format=fmt, size=size, spice_level=spice, initial_tech=tech,
                              difficulty=enemy, seed=seed, variant=args.variant, sides=sides,
                              base=args.base, army=args.army, city=args.city, limit=minutes,
                              style=args.style or 'default', mod=args.mod, minutes=minutes,
                              skirmish=spec, candidate_house=HOUSE_IDS[houses[seat]],
                              candidate_seat='swapped' if swapped else 'original',
                              candidate_style=getattr(args, 'candidate_style', None) or '',
                              sample_case=index + 1, sample_seed=sample_seed,
                              require_research=args.mod == 'dunecity'))
    return games


def sample_schedule(args):
    if getattr(args, 'case_manifest', None):
        return manifest_sample_schedule(args)
    games = []
    mods = ('vanilla', 'dunecity') if args.mod == 'both' else (args.mod,)
    for mod in mods:
        minutes = game_limit(args, mod)
        rng = random.Random(f'{args.sample_seed}/{mod}')
        factors = dict(SAMPLE_FACTORS, spice_level=tuple(SPICE)[int(mod == 'vanilla'):],
                       initial_tech=tuple(range(3 if mod == 'vanilla' else 1, 10)))
        if getattr(args, 'sample_formats', None) is not None:
            factors['format'] = tuple(args.sample_formats)
        draws = {factor: balanced_draw(levels, args.cases, rng) for factor, levels in factors.items()}
        for index in range(args.cases):
            case = {factor: draw[index] for factor, draw in draws.items()}
            if getattr(args, 'sample_enemy', None):
                case['difficulty'] = args.sample_enemy
            seed = rng.getrandbits(32)
            fmt, enemy = case['format'], case['difficulty']
            count = 2 if fmt == '1v1' else 6 if fmt == 'ffa6' else 4
            houses = list(HOUSES[:count])
            shift = index % count
            houses = houses[shift:] + houses[:shift]
            teams = [1, 1, 2, 2] if fmt == '2v2' else list(range(1, count + 1))
            size, spice, tech = case['size'], case['spice_level'], case['initial_tech']
            spec = (f'size={size},base={args.base},army={args.army},city={args.city},'
                    f'spice={SPICE[spice]},sand=65,rock=30,mountain=5,'
                    f'tech={tech},credits=3000,seed={seed}')
            cell = (f'sample/{mod}/s{args.sample_seed}-n{args.cases}/{args.base}-a{args.army}-c{args.city}/'
                    f'{index + 1:03d}/{size}/{spice}/{fmt}/{enemy}/t{tech}/{minutes}m/{seed}')
            # Only the candidate controller moves. Houses, starts, map and enemy tier
            # stay fixed; in 2v2 the other three seats (including its ally) use that tier.
            for swapped, seat in enumerate((0, 2 if fmt == '2v2' else count - 1)):
                sides = [(house, team, 'brutal' if i == seat else enemy)
                         for i, (house, team) in enumerate(zip(houses, teams))]
                games.append(dict(id=f'{cell}/{"swap" if swapped else "original"}', **case,
                                  seed=seed, variant=args.variant, sides=sides, base=args.base, army=args.army,
                                  city=args.city, limit=minutes, style=args.style or 'default', mod=mod,
                                  minutes=minutes, skirmish=spec, candidate_house=HOUSE_IDS[houses[seat]],
                                  candidate_seat='swapped' if swapped else 'original',
                                  candidate_style=getattr(args, 'candidate_style', None) or '',
                                  sample_case=index + 1, sample_seed=args.sample_seed,
                                  require_research=mod == 'dunecity'))
    return games


def shard(text):
    try:
        k, n = (int(part) for part in text.split('/'))
        if 1 <= k <= n:
            return k, n
    except ValueError:
        pass
    raise argparse.ArgumentTypeError('--shard K/N with 1 <= K <= N')


def seat_styles_argument(text):
    names = tuple(name.strip() for name in text.split(','))
    if not 2 <= len(names) <= 6 or 'control' not in names or any(not name for name in names):
        raise argparse.ArgumentTypeError('--seat-styles needs control plus 1-5 styles (or 2-6 controls)')
    variants = [name for name in names if name != 'control']
    if len(variants) != len(set(variants)):
        raise argparse.ArgumentTypeError('--seat-styles repeats a non-control variant')
    for name in variants:
        if Path(name).name != name or not (root / 'tests/ai/play-styles' / (name + '.ini')).is_file():
            raise argparse.ArgumentTypeError('--seat-styles must name existing play-style INIs or control')
    return names


def seat_style_schedule(args, games):
    names = getattr(args, 'seat_styles', None)
    if not names:
        return games
    variants = [name for name in names if name != 'control']
    scheduled = []
    for game in games:
        seats = len(game['sides'])
        if not variants:
            # Native all-control remains exactly today's schedule, including IDs and both candidate seats.
            scheduled.append(dict(game, seat_styles={str(HOUSE_IDS[house]): 'control'
                                                     for house, _, _ in game['sides']},
                                  style_cohort='control', style_rotation=0))
            continue
        if args.mode == 'sample' and game['candidate_seat'] != 'original':
            continue  # The full rotation, not a second singled-out candidate, supplies the seat control.
        for first in range(0, len(variants), seats - 1):
            cohort = variants[first:first + seats - 1]
            labels = ['control'] + cohort
            labels += ['control'] * (seats - len(labels))
            identity = hashlib.sha256(','.join(labels).encode()).hexdigest()[:12]
            for rotation in range(seats):
                assignments = {str(HOUSE_IDS[house]): labels[(seat + rotation) % seats]
                               for seat, (house, _, _) in enumerate(game['sides'])}
                scheduled.append(dict(game, id=f'{game["id"]}/seat-styles/{identity}/r{rotation}',
                                      seat_styles=assignments, style_cohort=','.join(cohort),
                                      style_rotation=rotation))
    return scheduled


def schedule(args):
    games = sample_schedule(args) if args.mode == 'sample' else matrix_schedule(args)
    games = seat_style_schedule(args, games)
    if args.shard:
        k, n = args.shard
        games = games[k - 1::n]
    return games




def matrix_schedule(args):
    games = []
    mod = getattr(args, 'mod', 'dunecity')
    minutes = game_limit(args, mod)
    for size in args.sizes:
        for spice in args.spice:
            for fmt in args.formats:
                difficulties = [(level,) for level in args.difficulties] if args.mode == 'matrix' else [
                    ('brutal', 'hard'), ('hard', 'medium'), ('medium', 'easy')]
                if args.mode == 'ladder' and fmt != '1v1':
                    continue
                for levels in difficulties:
                    for index, seed in enumerate(args.seeds):
                        for swapped in range(2 if args.mode == 'ladder' else 1):
                            count = 2 if fmt == '1v1' else 6 if fmt == 'ffa6' else 4
                            houses = list(HOUSES[:count])
                            shift = index % count
                            houses = houses[shift:] + houses[:shift]
                            if swapped:
                                houses.reverse()
                            teams = [1, 1, 2, 2] if fmt == '2v2' else list(range(1, count + 1))
                            sides = [(house, team, levels[i] if len(levels) > 1 else levels[0])
                                     for i, (house, team) in enumerate(zip(houses, teams))]
                            difficulty = '-v-'.join(levels)
                            cell = f'{args.mode}/{args.base}-a{args.army}-c{args.city}/{size}/{spice}/{fmt}/{difficulty}/{minutes}m/{seed}'
                            if args.tech != 9:
                                cell += f'/t{args.tech}'
                            if mod != 'dunecity':
                                cell = f'{mod}/{cell}'
                            if swapped:
                                cell += '/swap'
                            games.append(dict(id=cell, seed=seed, size=size, spice_level=spice, format=fmt,
                                              difficulty=difficulty, variant=args.variant, sides=sides, base=args.base, army=args.army,
                                              city=args.city, limit=minutes, style=args.style or 'default', mod=mod, initial_tech=args.tech,
                                              minutes=minutes, skirmish=f'size={size},base={args.base},army={args.army},'
                                              f'city={args.city},spice={SPICE[spice]},sand=65,rock=30,mountain=5,'
                                              f'tech={args.tech},credits=3000,seed={seed}'))
    return games


def score_match(game, result, final):
    """Audit-only population adjudication; the engine result and eliminations stay untouched."""
    ids = [int(house['house']) for house in final]
    eliminated = {int(house['house']): float(house['eliminated_s']) for house in final if house['alive'] == '0'}
    teams = {int(house['house']): int(house['team']) for house in final}
    raw_scores = match_score.score_game(ids, eliminated, teams=teams)
    winner = int(result.get('winner_team') or 0)
    condition = 'conquest' if winner > 0 else 'draw'
    rule = 'conquest-placement'
    populations = defaultdict(int)
    if game.get('mod', 'dunecity') == 'dunecity':
        rule = 'city-population-20pct'
        required = {HOUSE_IDS[house] for house, _, _ in game['sides']}
        if set(ids) != required or len(ids) != len(required):
            raise ValueError('population_house_roster_incomplete')
        for house in final:
            population = int(house['population'])
            if population < 0:
                raise ValueError('population_not_measured')
            populations[int(house['team'])] += population
        if result.get('outcome') == 'time_limit' and len(populations) >= 2:
            ranked = sorted(populations, key=populations.get, reverse=True)
            leader, runner_up = ranked[:2]
            # Integer arithmetic makes exactly 20% a win, without rounding a
            # smaller lead up to the boundary. Zero versus zero remains a draw.
            if populations[leader] > 0 and 5 * populations[leader] >= 6 * populations[runner_up]:
                winner, condition = leader, 'population'
    scores = raw_scores
    if condition == 'population':
        # Rank the population winner first. Other surviving teams share the
        # next places; teams already eliminated retain their relative order.
        scored_eliminated = {house: time for house, time in eliminated.items() if teams[house] != winner}
        for house in ids:
            if teams[house] != winner:
                scored_eliminated.setdefault(house, float(result['end_s']))
        scores = match_score.score_game(ids, scored_eliminated, teams=teams)
    return (dict(scoring_rule=rule, scored_winner_team=winner, win_condition=condition,
                 team_populations=json.dumps(dict(sorted(populations.items())), separators=(',', ':'))),
            raw_scores, scores)


def detect(samples, observer=True):
    """Per-house flags and eligibility; observer-off audit metrics are unmeasured."""
    if not observer:
        for sample in samples:
            sample['flags'] = ''
        return {field: value for detector in DETECTORS for field, value in (
            (detector, 0), ('eligible_' + detector, 0), ('first_' + detector, ''))}
    first, eligible, since, previous = {}, set(), {}, None
    seen = {'starport': False, 'radar': False, 'heavy': False, 'ix': False}
    for sample in sorted(samples, key=lambda row: int(row['cycle'])):
        s = {key: int(value) for key, value in sample.items()
             if key not in META and key not in ('fronts', 'flags', 'boom_phase') and value != ''}
        minute, cycle = s['minute'], s['cycle']
        alive = bool(s['alive'])
        city = alive and s['yards'] > 0
        old = previous or s
        growth = s['built'] > old['built'] or s['population'] > old['population']
        under_attack = s['enemies'] >= 3 and (s['structures_lost'] > old['structures_lost']
                                            or s['base_hp'] < old['base_hp'])
        for item in seen:
            seen[item] |= s[item] > 0 or s.get(item + '_built', 0) > 0
        predicates = {
            'economy_stall': (city and s['credits'] < 1000 and not growth, 4, city and minute >= 4),
            'power_deficit': (city and s['power'] < s['need'], 4, city and minute >= 4),
            'no_ground_defence': (city and minute >= 5 and s['enemies'] >= 3 and s['covered_enemies'] == 0,
                                  2, city and minute >= 5 and s['enemies'] >= 3),
            'idle_under_attack': (alive and under_attack and s['fighters'] >= 3 and s['moving'] == 0
                                  and s['targeting'] == 0, 2, alive and under_attack and s['fighters'] >= 3),
            'unused_credits': (city and minute >= 5 and s['credits'] >= 5000 and s['queue'] == 0
                               and s['upgrading'] == 0 and not growth, 4, city and minute >= 9),
            'bankrupt_queue_empty': (city and s['credits'] < 100 and s['queue'] == 0 and s['upgrading'] == 0,
                                     2, city and minute >= 2),
            'zero_harvesters': (city and minute >= 5 and s['spice'] > 0 and s['harvesters'] == 0,
                                4, city and minute >= 5 and s['spice'] > 0),
            'storage_full': (s.get('storage_overflow_cycles', 0) > old.get('storage_overflow_cycles', 0),
                             0, alive and s.get('ledger_enabled') == 1 and 'storage_overflow_cycles' in s),
        }
        for item in seen:
            name = {'heavy': 'missing_heavy_factory', 'ix': 'missing_ix'}.get(item, 'missing_' + item)
            predicates[name] = (city and minute >= 20 and not seen[item], 0, city and minute >= 20)
        predicates['early_no_fight'] = (not alive and minute < 10 and s['kill_value'] < 500, 0, True)
        flags = []
        for name, (condition, duration, exposed) in predicates.items():
            if exposed:
                eligible.add(name)
            if not condition:
                since.pop(name, None)
                continue
            since.setdefault(name, cycle)
            if cycle - since[name] >= duration * CYCLES_PER_MINUTE:
                first.setdefault(name, minute)
                flags.append(name)
        sample['flags'] = '/'.join(flags)
        previous = s
    return {field: value for detector in DETECTORS for field, value in (
        (detector, int(detector in first)), ('eligible_' + detector, int(detector in eligible)),
        ('first_' + detector, first.get(detector, '')))}
def seat_style_assignments(game):
    if game.get('seat_styles'):
        return game['seat_styles']
    if game.get('candidate_style'):
        return {str(game['candidate_house']): game['candidate_style']}
    return {}


def seat_style_inputs(args, game, out):
    """Constant per-house config overlays; no genome search or within-game phase changes."""
    if getattr(args, 'candidate_style', None) and (args.mode != 'sample' or args.style):
        raise ValueError('--candidate-style requires --mode sample and cannot be combined with --style')
    assignments = {house: style for house, style in seat_style_assignments(game).items() if style != 'control'}
    if not assignments:
        return None
    prefix = 'seat' if game.get('seat_styles') else 'candidate'
    directory = out.with_suffix('.candidate')
    directory.mkdir(parents=True, exist_ok=True)
    schedule = '[Schedule]\nopening=0\nmid=9999\nlate=10000\n\n[Genomes]\n'
    for house, style in assignments.items():
        source = root / 'tests/ai/play-styles' / (style + '.ini')
        if not style or Path(style).name != style or not source.is_file():
            raise ValueError('Per-seat styles must name an existing play-style INI')
        schedule += f'{house}={prefix}-{style}\n'
        payload = source.read_bytes()
        for phase in ('opening', 'mid', 'late'):
            (directory / f'{house}-{phase}.ini').write_bytes(payload)
    (directory / 'schedule.ini').write_text(schedule, encoding='utf-8')
    return directory




def command(args, binary, game, out):
    sides = ','.join(f'{house}:{team}:{CLASSES[level]}' for house, team, level in game['sides'])
    tracked = next((house for house, _, _ in game['sides']
                    if HOUSE_IDS[house] == game.get('candidate_house')), game['sides'][0][0])
    cmd = [sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
           '--prebuilt-probe', str(binary), '--output-dir', str(out), '--scenario', '--mod', game.get('mod', 'dunecity'),
           '--seed', str(game['seed']), '--minutes', str(game['minutes']),
           '--capture-mib', str(getattr(args, 'capture_mib', 256)),
           '--wall-timeout', str(args.wall_timeout), '--path-workers', '1', '--house', tracked,
           '--scenario-sides', sides, '--local-spectator', '--skirmish', game['skirmish']]
    cmd += headless_asset_arguments()
    if not (args.attack_events or getattr(args, 'keep_telemetry', False)):
        cmd += ['--no-ai-telemetry']
    if game.get('require_research'):
        # Overlay the ordinary rule, even at tech 9. Never grant a lab or lower
        # the requested technology to make a research-disabled game look valid.
        options = configparser.ConfigParser(strict=False, interpolation=None)
        options.optionxform = str
        if args.game_options:
            with Path(args.game_options).open() as handle:
                options.read_file(handle)
        if not options.has_section('Game Options'):
            options.add_section('Game Options')
        options.set('Game Options', 'Research Lab', 'true')
        path = out.with_suffix('.game-options.ini')
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('w') as handle:
            options.write(handle)
        cmd += ['--game-options', str(path)]
    if args.game_options and not game.get('require_research'):
        cmd += ['--game-options', str(args.game_options)]
    if args.style:
        cmd += ['--scenario-style', args.style]
    if directory := seat_style_inputs(args, game, out):
        cmd += ['--genome-phase-dir', str(directory)]
    return cmd


MAP_EXPORT_FIELDS = ('id', 'sample_case', 'candidate_seat', 'artifact', 'sha256', 'bytes',
                     'chunk', 'chunks', 'base64')


def export_maps(args, out, game, cmd, setup, generated, summary, probe_sha256):
    """Persist exact bytes and queue-collectable CSV receipts before scratch cleanup."""
    directory = Path(args.export_maps)
    if not directory.is_absolute():
        directory = Path(args.results) / directory
    destination = directory / hashlib.sha256(game['id'].encode()).hexdigest()
    source = out / 'export-map.ini'
    if not source.is_file() or not source.stat().st_size:
        raise RuntimeError(f'Map export missing for {game["id"]}')
    artifacts = {'map.ini': source.read_bytes()}
    options_source = out / 'export-options.ini'
    if not options_source.is_file() or not options_source.stat().st_size:
        raise RuntimeError(f'Effective options export missing for {game["id"]}')
    artifacts['effective-options.ini'] = options_source.read_bytes()
    if '--game-options' in cmd:
        artifacts['requested-game-options.ini'] = Path(cmd[cmd.index('--game-options') + 1]).read_bytes()
    for path in sorted((out / 'profile').rglob('*.ini')):
        relative = path.relative_to(out / 'profile')
        if not {'mods', 'workshop'}.intersection(relative.parts):
            artifacts[(Path('profile') / relative).as_posix()] = path.read_bytes()
    provenance = dict(schema_version=1, game=game, command=cmd, setup=setup, generated=generated,
                      summary=summary, probe_sha256=probe_sha256,
                      settings_note='Profile INIs and requested overlay retained; setup hashes are effective engine receipts.',
                      artifacts={name: dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
                                 for name, data in artifacts.items()})
    artifacts['provenance.json'] = json.dumps(provenance, sort_keys=True, indent=2).encode('utf-8')
    rows = []
    for name, data in artifacts.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        # Bound CSV fields below Python's default field-size limit.
        chunks = [data[index:index + 32768] for index in range(0, len(data), 32768)] or [b'']
        for index, chunk in enumerate(chunks):
            rows.append(dict(id=game['id'], sample_case=game.get('sample_case', ''),
                             candidate_seat=game.get('candidate_seat', ''), artifact=name,
                             sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                             chunk=index, chunks=len(chunks), base64=base64.b64encode(chunk).decode('ascii')))
    append(Path(args.results) / 'export-maps.csv', MAP_EXPORT_FIELDS, rows)


def retain_telemetry(results, out):
    """Copy only durable diagnostics before ordinary managed-profile cleanup."""
    relative = Path('diagnostics') / out.name
    destination = results / relative
    paths = [out / 'run.log', out / 'summary.json'] + sorted((out / 'profile').rglob('events.jsonl'))
    for source in paths:
        if source.is_file():
            target = destination / source.relative_to(out)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    return relative.as_posix()


def research_verified(game, setup, lines):
    if not game.get('require_research'):
        return True
    observed = [parse(line, 'DUNECITY_TECH_SAMPLE:') for line in lines if 'DUNECITY_TECH_SAMPLE:' in line]
    initial = {int(item['house']): item for item in observed if item.get('cycle') == '0'}
    required = {HOUSE_IDS[house] for house, _, _ in game['sides']}
    return (setup.get('research_enabled') == '1' and set(initial) == required
            and all(item.get('research_enabled') == '1' for item in observed))
def seat_styles_verified(game, setup, configs, lines):
    assignments = seat_style_assignments(game)
    if not assignments:
        return True
    native = setup.get('config_hash')
    observed = {row.get('house'): row.get('config_hash') for row in configs}
    phases = [parse(line, 'GENOME_PHASE:') for line in lines if 'GENOME_PHASE:' in line]
    expected = {str(HOUSE_IDS[name]) for name, _, _ in game['sides']}
    overlaid = {house: style for house, style in assignments.items() if style != 'control'}
    if (not native or setup.get('style') != 'default' or set(observed) != expected
            or not set(assignments) <= expected
            or any(observed[house] != native for house in expected - overlaid.keys())
            or len(phases) != len(overlaid) or {row.get('house') for row in phases} != set(overlaid)):
        return False
    prefix = 'seat' if game.get('seat_styles') else 'candidate'
    return all(row.get('phase') == 'opening' and row.get('cycle') == '0'
               and row.get('genome') == prefix + '-' + overlaid[row['house']]
               and row.get('config_hash') == observed[row['house']] for row in phases)




def play(args, scratch, binary, game):
    out = scratch / ('g' + hashlib.sha256(game['id'].encode()).hexdigest()[:12])
    telemetry = args.attack_events or getattr(args, 'keep_telemetry', False)
    probe_sha256 = hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    started = time.monotonic()
    cmd = command(args, binary, game, out)
    assert '--army-deploys-regression-observer' not in cmd
    observer_off = getattr(args, 'observer_off', False)
    env = dict(os.environ)
    env.pop('BALANCE_ARMY_DEPLOYS_REGRESSION_OBSERVER', None)
    env.pop('DUNECITY_TECH_SAVINGS_AUDIT', None)
    if observer_off:
        env.pop('BALANCE_DUNECITY_AUDIT', None)
    else:
        env['BALANCE_DUNECITY_AUDIT'] = '1'
    assert 'BALANCE_ARMY_DEPLOYS_REGRESSION_OBSERVER' not in env
    if getattr(args, 'candidate_style', None) or getattr(args, 'seat_styles', None):
        for key in ('BALANCE_SCENARIO_STYLE', 'BALANCE_GENOME_PHASE_DIR', 'DUNECITY_QUANTBOT_OVERLAY'):
            env.pop(key, None)
    if getattr(args, 'export_maps', None):
        env['BALANCE_SCENARIO_DUMP_MAP'] = str((out / 'export-map.ini').resolve())
        env['BALANCE_SCENARIO_DUMP_OPTIONS'] = str((out / 'export-options.ini').resolve())
    process = subprocess.run(cmd, cwd=root, capture_output=True, text=True, env=env)
    log = out / 'run.log'
    lines = log.read_text(errors='replace').splitlines() if log.exists() else []
    result = next((parse(line, 'SCENARIO_RESULT:') for line in lines if 'SCENARIO_RESULT:' in line), {})
    final = [parse(line, 'SCENARIO_HOUSE:') for line in lines if 'SCENARIO_HOUSE:' in line]
    generated = next((parse(line, 'SCENARIO_MAP:') for line in lines if 'SCENARIO_MAP:' in line), {})
    timing = next((parse(line, 'PROBE_TIMING:') for line in lines if 'PROBE_TIMING:' in line), {})
    setup = next((parse(line, 'SCENARIO_SETUP:') for line in lines if 'SCENARIO_SETUP:' in line), {})
    configs = [parse(line, 'SCENARIO_AI_CONFIG:') for line in lines if 'SCENARIO_AI_CONFIG:' in line]
    summary_path = out / 'summary.json'
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    digest = next((parse(line, 'PROBE_DIGEST:').get('digest', '') for line in lines
                   if 'PROBE_DIGEST:' in line and 'final=1' in line), '')
    merged = defaultdict(dict)
    for line in lines:
        for marker in ('ELIMINATION_SAMPLE:', 'DUNECITY_AUDIT:', 'DUNECITY_ECONOMY:', 'DUNECITY_TROOPERS:'):
            if marker in line:
                data = parse(line, marker)
                data.update(json.loads(data.pop('economy', '{}')))
                merged[(int(data['house']), int(data['cycle']))].update(data)
    fleet_samples, refinery_samples = fleet_observations(lines)
    for key, data in fleet_samples.items():
        merged[key].update(data)
    samples = [dict(metadata(game), **data) for _, data in sorted(merged.items())]
    capture = air_kill_capture(out, final, max((int(sample['cycle']) for sample in samples), default=0),
                               telemetry)
    add_air_kill_samples(samples, capture)
    # A projectile may kill after its firing house was eliminated. Final-house
    # totals consume the whole capture, not only that house's last live sample.
    air_cycle = max((int(event['cycle']) for event in capture['events']), default=0)
    air_final = {int(house['house']): dict(house=house['house'], cycle=air_cycle) for house in final}
    add_air_kill_samples(list(air_final.values()), capture)
    observer_seen = any('DUNECITY_AUDIT:' in line for line in lines)
    missing = (not observer_off and (not samples or any(
        'power' not in sample or 'fleet_steps' not in sample for sample in samples)))
    wrong_observer = observer_off and observer_seen
    error = '' if process.returncode == 0 and result and samples and not missing and not wrong_observer else f'rc={process.returncode};incomplete={int(missing)}'
    if wrong_observer:
        error = ';'.join(filter(None, (error, 'observer_mode_not_verified')))
    research = setup.get('research_enabled', '')
    if not research_verified(game, setup, lines):
        error = ';'.join(filter(None, (error, 'research_enabled_not_verified')))
    if not seat_styles_verified(game, setup, configs, lines):
        error = ';'.join(filter(None, (error, 'seat_styles_not_verified')))
    if 'candidate_house' in game and str(game['candidate_house']) not in {house['house'] for house in final}:
        error = ';'.join(filter(None, (error, 'candidate_result_missing')))
    scored = {}
    if not error:
        try:
            scored, raw_scores, scores = score_match(game, result, final)
        except (KeyError, ValueError) as exc:
            error = f'outcome_not_verified:{exc}'
    row = dict(metadata(game), sides=','.join(f'{h}:{t}:{d}' for h, t, d in game['sides']),
               map_seed=generated.get('map_seed', ''), terrain_attempts=generated.get('attempts', ''),
               skipped=generated.get('skipped', ''), outcome=result.get('outcome', ''), winner_team=result.get('winner_team', ''),
               minutes=result.get('minutes', ''), loop_ms=timing.get('loop_ms', ''),
               wall_s=round(time.monotonic() - started, 3), digest=digest, attack_telemetry=int(telemetry), error=error)
    row.update(mod=game.get('mod', 'dunecity'), research_enabled=research,
               **{field: game.get(field, '') for field in
                  ('candidate_house', 'candidate_seat', 'candidate_style', 'sample_case', 'sample_seed')})
    row.update(seat_styles=json.dumps(game.get('seat_styles', {}), sort_keys=True) if game.get('seat_styles') else '',
               style_cohort=game.get('style_cohort', ''), style_rotation=game.get('style_rotation', ''),
               observer_mode='off' if observer_off else 'on')
    row.update(fleet_observer_status='off' if observer_off else 'complete' if not missing else 'incomplete',
               fleet_house_samples=len(fleet_samples), fleet_refinery_samples=len(refinery_samples))
    row.update({field: setup.get(field, '') for field in (
        'config_hash', 'options_hash', 'map_hash', 'content_hash', 'object_data_hash')})
    row.update(ai_config_hashes=','.join(f'{item["house"]}:{item["config_hash"]}' for item in configs),
               probe_sha256=probe_sha256, source_commit=summary.get('sourceCommit', ''),
               source_fingerprint=summary.get('sourceFingerprint', ''),
               source_fingerprint_error=summary.get('sourceFingerprintError', ''))
    row.update(air_kill_telemetry=capture['status'], air_kill_telemetry_reasons=capture['reasons'],
               air_kill_attribution=capture['attribution'], air_kill_unknown_attribution=capture['unknown_attribution'],
               capture_mib=getattr(args, 'capture_mib', 256), diagnostics='')
    row.update(scored)
    results = Path(args.results)
    if getattr(args, 'export_maps', None):
        try:
            export_maps(args, out, game, cmd, setup, generated, summary, probe_sha256)
        except (OSError, RuntimeError) as exc:
            error = ';'.join(filter(None, (error, f'map_export_failed:{exc}')))
            row['error'] = error
    if error:
        (results / f'failed-{out.name}.txt').write_text(game['id'] + '\n' + process.stdout[-4000:] + '\n'
                                                       + process.stderr[-4000:] + '\n' + '\n'.join(lines[-80:]))
    else:
        append(results / 'map-features.csv', MAP_FEATURE_FIELDS,
               rows_from_log(lines, game, setup))
        groups = defaultdict(list)
        for sample in samples:
            groups[int(sample['house'])].append(sample)
        house_rows = []
        difficulty_of = {HOUSE_IDS[h]: d for h, _, d in game['sides']}
        attacks = defaultdict(list)
        attack_rows = []
        if telemetry:
            for event in summary['attacks']:
                if int(event['data']['members']) <= 0:
                    continue
                house = int(event['house'])
                cycle = int(event['cycle'])
                attacks[house].append(cycle / CYCLES_PER_MINUTE)
                attack_rows.append(dict(metadata(game), house=house, cycle=cycle,
                                        members=event['data']['members'], value=event['data']['value']))
        for house in final:
            number = int(house['house'])
            detectors = detect(groups[number], observer=not observer_off)
            last = max(groups[number], key=lambda row: int(row['cycle']))
            house_rows.append(dict(metadata(game), **{key: house[key] for key in (
                'house', 'team', 'alive', 'eliminated_s', 'kill_value', 'loss_value', 'structures',
                'army_value', 'army_units', 'sortie_minutes')}, **detectors,
                score=round(scores[number].score, 4), drawn=int(scores[number].drawn),
                won=int(int(house['team']) == scored['scored_winner_team']), difficulty=difficulty_of[number],
                population=house.get('population', ''),
                raw_score=round(raw_scores[number].score, 4), raw_drawn=int(raw_scores[number].drawn),
                raw_won=int(house['team'] == result.get('winner_team')),
                seat_style=seat_style_assignments(game).get(str(number), 'control'),
                attack_telemetry=int(telemetry),
                attack_count=len(attacks[number]) if telemetry else '',
                first_attack_minute=min(attacks[number]) if attacks[number] else '',
                air_kill_telemetry=capture['status'],
                **{key: last.get(key, '') for key in TROOPER_FIELDS},
                **{key: last.get(key, '') for key in ('harvesters', 'refineries') + FLEET_VALUES},
                **{key: air_final[number][key] for key in AIR_ATTRIBUTION_FIELDS + AIR_COUNT_FIELDS},
                **{key: last.get(key, '') if telemetry or key == 'storage_full_cycles' else ''
                   for key in ('storage_full_cycles', 'storage_overflow_cycles', 'storage_income_lost')},
                flags='/'.join(d for d in DETECTORS if detectors[d])))
        append(results / 'refinery-samples.csv', REFINERY_FIELDS,
               [dict(metadata(game), **sample) for sample in refinery_samples])
        append(results / 'samples.csv', SAMPLE_FIELDS, samples)
        append(results / 'houses.csv', HOUSE_FIELDS, house_rows)
        if telemetry:
            append(results / 'waves.csv', ATTACK_FIELDS, attack_rows)
            append(results / 'air-kills.csv', AIR_KILL_FIELDS,
                   [dict(metadata(game), **event) for event in capture['events']])
        supplemental = ({name: [] for name, _ in TECH_TABLES} if observer_off
                        else tech_audit(lines, samples, final, capture))
        for filename, fields in TECH_TABLES:
            append(results / filename, fields, [dict(metadata(game), mod=game.get('mod', 'dunecity'), **entry,
                                                   difficulty=difficulty_of[int(entry['house'])])
                                               for entry in supplemental[filename]])
        if getattr(args, 'keep_telemetry', False):
            row['diagnostics'] = retain_telemetry(results, out)
    append(results / 'games.csv', GAME_FIELDS, [row])
    if not error:
        shutil.rmtree(out.with_suffix('.candidate'), ignore_errors=True)
    finish_game(scratch, out, bool(error))
    return row


def build_probe(args, scratch):
    if args.prebuilt_probe:
        return args.prebuilt_probe
    out = scratch / 'probe'
    remove_tree(out)
    subprocess.run([sys.executable, str(root / 'tests/ai/run-campaign-balance.py'), '--build-dir', str(args.build_dir),
                    '--output-dir', str(out), '--scenario', '--mod', 'vanilla' if args.mod == 'both' else args.mod, '--skirmish',
                    'size=64,base=cy,army=0,spice=0', '--scenario-sides',
                    'harkonnen:1:qBotEasy,atreides:2:qBotEasy', '--house', 'harkonnen', '--local-spectator',
                    '--minutes', '1', '--capture-mib', str(getattr(args, 'capture_mib', 256)), '--no-ai-telemetry',
                    '--build-only'],
                   # Prepare only: no unrequested warm-up game outside the sampled schedule.
                   cwd=root, check=True, capture_output=True, text=True)
    remove_tree(out / 'profile')
    return probe_binary(out)


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    for name, fields in (('games.csv', GAME_FIELDS), ('houses.csv', HOUSE_FIELDS),
                         ('samples.csv', SAMPLE_FIELDS), ('waves.csv', ATTACK_FIELDS),
                         ('air-kills.csv', AIR_KILL_FIELDS),
                         ('map-features.csv', MAP_FEATURE_FIELDS),
                         ('refinery-samples.csv', REFINERY_FIELDS)) + TECH_TABLES:
        require_schema(results / name, fields)
    observer_mode = 'off' if getattr(args, 'observer_off', False) else 'on'
    if any(row.get('observer_mode') != observer_mode for row in read(results / 'games.csv')):
        raise ValueError('Observer mode changed; use a new results directory')
    exported = {row['id'] for row in read(results / 'export-maps.csv')
                if row['artifact'] == 'map.ini'} if getattr(args, 'export_maps', None) else None
    done = {r['id'] for r in read(results / 'games.csv') if not r['error']
            and (not (args.attack_events or args.keep_telemetry) or r.get('attack_telemetry') == '1')
            and (not args.keep_telemetry or r.get('diagnostics') and (results / r['diagnostics']).is_dir())
            and (exported is None or r['id'] in exported)}
    todo = [game for game in schedule(args) if game['id'] not in done]
    print(f'{len(todo)} games to play ({len(done)} complete)', flush=True)
    if todo:
        require_free_space()
        with scratch_dir('dunecity-ai-audit', args.scratch) as scratch:
            binary = build_probe(args, scratch)
            with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                futures = [pool.submit(play, args, scratch, binary, game) for game in todo]
                for n, future in enumerate(as_completed(futures), 1):
                    row = future.result()
                    print(f'[{n}/{len(todo)}] {row["id"]}: {row["minutes"]} min {row["error"]}', flush=True)
    report([results])
    if args.mode == 'sample':
        selected = {game['id'] for game in schedule(args)}
        rows = {row['id']: row for row in read(results / 'games.csv')}
        if any(game_id not in rows or rows[game_id]['error'] for game_id in selected):
            raise SystemExit(1)


def load(directories, name, keys):
    rows = {}
    for directory in directories:
        for row in read(Path(directory) / name):
            rows[tuple(row[key] for key in keys)] = row
    return list(rows.values())


def sample_groups(games):
    groups = defaultdict(list)
    for game in games:
        if game.get('candidate_house', '') == '':
            continue
        groups[(game['mod'], 'all', 'all')].append(game)
        for factor in SAMPLE_STRATA:
            groups[(game['mod'], factor, str(game[factor]))].append(game)
    return groups


def sample_coverage(games):
    """List-mode coverage counts cases once, then shows both candidate seats."""
    for mod in ('vanilla', 'dunecity'):
        rows = [game for game in games if game['mod'] == mod]
        if not rows:
            continue
        cases = {game['sample_case']: game for game in rows}
        print(f'{mod}: {len(cases)} sampled cases / {len(rows)} games')
        for factor in SAMPLE_STRATA[:-1]:
            counts = defaultdict(int)
            for game in cases.values():
                counts[game[factor]] += 1
            print(f'  {factor}: ' + ', '.join(f'{level}={count}' for level, count in sorted(counts.items())))
        seats = defaultdict(int)
        for game in rows:
            seats[game['candidate_seat']] += 1
        print('  candidate seats: ' + ', '.join(f'{seat}={count}' for seat, count in sorted(seats.items())))
        if mod == 'dunecity':
            print('  research: required at every initial tech level, including 9; verified from engine telemetry at run time')


def sample_report(games, houses):
    by_house = {(row['id'], row['house']): row for row in houses}
    print('Sampler: one Brutal candidate seat; difficulty strata describe the other controllers. '
          'Errors/missing candidate evidence are unmeasured, never counted as losses or zero scores.')
    print('DuneCity time limits: a team wins with population >= 1.2 times every rival; '
          'raw conquest outcomes/draws remain available separately. Classic scoring is unchanged.')
    print('| Mod | Factor | Stratum | Games | Measured | Candidate wins | Conquest wins | Population wins | Draws | Raw draws | Mean score | Mean min | Research verified |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for (mod, factor, level), rows in sorted(sample_groups(games).items()):
        measured = [(game, by_house[(game['id'], str(game['candidate_house']))]) for game in rows
                    if not game['error'] and (game['id'], str(game['candidate_house'])) in by_house]
        wins = sum(house['won'] == '1' for _, house in measured)
        conquest = sum((house.get('raw_won') or house['won']) == '1' for _, house in measured)
        population = sum(house['won'] == '1' and game.get('win_condition') == 'population'
                         for game, house in measured)
        raw_draws = sum((house.get('raw_drawn') or house['drawn']) == '1' for _, house in measured)
        draws = sum(house['drawn'] == '1' for _, house in measured)
        score = f'{statistics.mean(float(house["score"]) for _, house in measured):+.3f}' if measured else 'unmeasured'
        minutes = f'{statistics.mean(float(game["minutes"]) for game, _ in measured):.2f}' if measured else 'unmeasured'
        research = f'{sum(game.get("research_enabled") == "1" for game, _ in measured)}/{len(rows)}' if mod == 'dunecity' else 'n/a'
        print(f'| {mod} | {factor} | {level} | {len(rows)} | {len(measured)} | {wins} | {conquest} | '
              f'{population} | {draws} | {raw_draws} | {score} | {minutes} | {research} |')


def observer_pair_error(before, after):
    if any(game.get('observer_mode') not in ('on', 'off') for game in (before, after)):
        return 'missing_observer_mode'
    return 'different_observer_mode' if before['observer_mode'] != after['observer_mode'] else ''


def require_matching_observers(previous, current):
    for game_id in sorted(previous.keys() & current.keys()):
        reason = observer_pair_error(previous[game_id], current[game_id])
        if reason:
            raise ValueError(f'Observer-mode pairing rejected for {game_id}: {reason}')


def sample_comparison(control, candidate):
    games = [{row['id']: row for row in load([directory], 'games.csv', ('id',))
              if row.get('candidate_house', '') != ''} for directory in (control, candidate)]
    require_matching_observers(*games)
    houses = [{(row['id'], row['house']): row for row in load([directory], 'houses.csv', ('id', 'house'))}
              for directory in (control, candidate)]
    common = games[0].keys() & games[1].keys()
    same = ('seed', 'mod', 'candidate_house', *SAMPLE_STRATA,
            'map_hash', 'options_hash', 'content_hash', 'object_data_hash')
    valid = [key for key in sorted(common) if all(not arm[key]['error'] for arm in games)
             and all(games[0][key].get(field, '') != '' and
                     games[0][key].get(field) == games[1][key].get(field) for field in same)
             and (games[0][key].get('scoring_rule') or 'conquest-placement')
                 == (games[1][key].get('scoring_rule') or 'conquest-placement')
             and all((key, arm[key]['candidate_house']) in source for arm, source in zip(games, houses))]
    print(f'Sampler matched candidate seats {len(valid)}/{len(common)} common; '
          f'control-only {len(games[0].keys() - games[1].keys())}, '
          f'candidate-only {len(games[1].keys() - games[0].keys())}. '
          'Pairing requires identical factors, map, options, content and scoring rule, not identical policy/source hashes.')
    print('Intervals resample sampled cases, keeping the two swapped seats together; '
          'no supported regression is not proof of equivalence.')
    print('| Mod | Factor | Stratum | Matched/available seats | Cases | Score control -> candidate | Paired delta [95% CI] | Candidate wins control -> candidate | Conquest wins | Population wins | Verdict |')
    print('|---|---|---|---|---|---|---|---|---|---|---|')
    matched = set(valid)
    available = {**games[0], **games[1]}
    for (mod, factor, level), group in sorted(sample_groups(available.values()).items()):
        rows = [game for game in group if game['id'] in matched]
        if not rows:
            print(f'| {mod} | {factor} | {level} | 0/{len(group)} | 0 | unmeasured | unmeasured | unmeasured | unmeasured | unmeasured | unmeasured |')
            continue
        by_case = defaultdict(list)
        for game in rows:
            key = (game['id'], game['candidate_house'])
            by_case[(game['sample_seed'], game['sample_case'])].append([source[key] for source in houses])
        pairs = [(statistics.mean(float(pair[1]['score']) for pair in group),
                  statistics.mean(float(pair[0]['score']) for pair in group),
                  all(pair[1]['drawn'] == '1' for pair in group),
                  all(pair[0]['drawn'] == '1' for pair in group)) for group in by_case.values()]
        result = match_score.compare(pairs, 9101)
        wins = [sum(source[(game['id'], game['candidate_house'])]['won'] == '1' for game in rows)
                for source in houses]
        conquest = [sum((source[(game['id'], game['candidate_house'])].get('raw_won')
                         or source[(game['id'], game['candidate_house'])]['won']) == '1' for game in rows)
                    for source in houses]
        population = [sum(source[(game['id'], game['candidate_house'])]['won'] == '1'
                          and arm[game['id']].get('win_condition') == 'population' for game in rows)
                      for arm, source in zip(games, houses)]
        print(f'| {mod} | {factor} | {level} | {len(rows)}/{len(group)} | {len(pairs)} | '
              f'{result.control:+.3f} -> {result.candidate:+.3f} | '
              f'{result.difference:+.3f} [{result.low:+.3f}, {result.high:+.3f}] | '
              f'{wins[0]} -> {wins[1]} | {conquest[0]} -> {conquest[1]} | '
              f'{population[0]} -> {population[1]} | {result.verdict} |')


def seat_style_report(games, houses):
    """Descriptive same-game comparisons; extra controls and rotations are not independent trials."""
    completed = {game['id']: game for game in games if game.get('seat_styles') and not game.get('error')}
    by_game = defaultdict(list)
    for house in houses:
        if house['id'] in completed:
            by_game[house['id']].append(house)
    groups = defaultdict(list)
    for game_id, game in completed.items():
        assignments = json.loads(game['seat_styles']) if isinstance(game['seat_styles'], str) else game['seat_styles']
        rows = by_game[game_id]
        controls = [row for row in rows if assignments.get(row['house']) == 'control']
        for row in rows:
            style = assignments.get(row['house'])
            if style is None:
                continue
            end = float(game['minutes'])
            eliminated = float(row['eliminated_s']) if row.get('eliminated_s', '') != '' else None
            survival = end if row['alive'] == '1' else (
                min(end, eliminated / 60) if eliminated is not None and eliminated >= 0 else None)
            order = (1, 0) if row['alive'] == '1' else (
                (0, eliminated) if eliminated is not None and eliminated >= 0 else None)
            finishing = []
            for control in controls:
                if control['house'] == row['house'] or control['team'] == row['team']:
                    continue
                when = float(control['eliminated_s']) if control.get('eliminated_s', '') != '' else None
                other = (1, 0) if control['alive'] == '1' else (
                    (0, when) if when is not None and when >= 0 else None)
                if order is not None and other is not None:
                    finishing.append(1 if order > other else -1 if order < other else 0)
            value = dict(row=row, survival=survival, finishing=finishing)
            groups[(game.get('mod', 'dunecity'), game['format'], game.get('style_cohort', ''), style)].append(value)
    print(f'Per-seat games complete {len(completed)}/{sum(bool(game.get("seat_styles")) for game in games)}. '
          'Each cohort rotates through every seat/start and controller tier. '
          'Scores/wins are team outcomes; survival and finishing order are per-house. '
          'Spare seats are native controls. Cohorts are reported separately; these are descriptive observations, '
          'not independent control pairs or an automatic default-on gate.')
    print('| Mod | Format | Cohort | Variant | House histories | Wins | Mean placement score | Survival min (measured) | Better/tied/worse than opposing control |')
    print('|---|---|---|---|---|---|---|---|---|')
    for (mod, fmt, cohort, style), entries in sorted(groups.items()):
        surviving = [value['survival'] for value in entries if value['survival'] is not None]
        survival = f'{statistics.mean(surviving):.2f} ({len(surviving)}/{len(entries)})' if surviving else 'unmeasured'
        finishing = [order for value in entries for order in value['finishing']]
        against = '/'.join(str(finishing.count(order)) for order in (1, 0, -1)) if finishing else 'unmeasured'
        print(f'| {mod} | {fmt} | {cohort} | {style} | {len(entries)} | '
              f'{sum(value["row"]["won"] == "1" for value in entries)} | '
              f'{statistics.mean(float(value["row"]["score"]) for value in entries):+.3f} | {survival} | {against} |')


def report(directories):
    games = load(directories, 'games.csv', ('id',))
    houses = load(directories, 'houses.csv', ('id', 'house'))
    if any(game.get('seat_styles') for game in games):
        seat_style_report(games, houses)
        return
    if any(game.get('candidate_house', '') != '' for game in games):
        sample_report(games, houses)
        return
    completed = [game for game in games if not game['error']]
    print(f'Games {len(completed)}/{len(games)}, houses {len(houses)}, '
          f'raw time limits {sum(game["winner_team"] == "0" for game in completed)}; '
          f'conquest wins {sum(int(game["winner_team"]) > 0 for game in completed)}; '
          f'population wins {sum(game.get("win_condition") == "population" for game in completed)}; '
          f'undecided {sum((game.get("scored_winner_team") or game["winner_team"]) == "0" for game in completed)}; '
          f'mean length {statistics.mean(float(g["minutes"]) for g in completed):.2f} min' if completed else 'No completed games')
    print('| Difficulty | Spice | Houses | ' + ' | '.join(DETECTORS) + ' |')
    print('|---|---|---|' + '---|' * len(DETECTORS))
    groups = defaultdict(list)
    for house in houses:
        groups[(house['difficulty'], house['spice_level'])].append(house)
    groups[('all', 'all')] = houses
    for (difficulty, spice), rows in sorted(groups.items()):
        cells = []
        for name in DETECTORS:
            hits = sum(int(row.get(name, 0)) for row in rows)
            exposed = sum(int(row.get('eligible_' + name, 0)) for row in rows)
            cells.append(f'{hits}/{exposed}')
        print(f'| {difficulty} | {spice} | {len(rows)} | ' + ' | '.join(cells) + ' |')
    for name in DETECTORS:
        examples = [f'{row["id"]}:h{row["house"]}@{row["first_" + name]}'
                    for row in houses if row.get(name) == '1']
        print(f'{name}: ' + ('; '.join(examples[:6]) if examples else 'none'))
    ladder = defaultdict(list)
    for game in completed:
        if game['id'].startswith('ladder/'):
            ladder[game['difficulty']].append(game)
    for pair, rows in sorted(ladder.items()):
        print(f'Ladder {pair}: stronger wins {sum((row.get("scored_winner_team") or row["winner_team"]) == "1" for row in rows)}/{len(rows)}, '
              f'weaker {sum((row.get("scored_winner_team") or row["winner_team"]) == "2" for row in rows)}, '
              f'draws {sum((row.get("scored_winner_team") or row["winner_team"]) == "0" for row in rows)}, '
              f'population wins {sum(row.get("win_condition") == "population" for row in rows)}')
    combat_report(directories)
    storage_report(directories)
    trooper_report(directories)
    tech_report(directories)


def checkpoint_samples(directories):
    samples = defaultdict(dict)
    for sample in load(directories, 'samples.csv', ('id', 'house', 'cycle')):
        cycle = int(sample['cycle'])
        if cycle in (10 * CYCLES_PER_MINUTE, 20 * CYCLES_PER_MINUTE):
            samples[(sample['id'], sample['house'])][cycle] = sample
    return samples


def checkpoint_value(house, samples, minute, field):
    point = samples[(house['id'], house['house'])].get(minute * CYCLES_PER_MINUTE)
    if point:
        return int(point[field])
    if house['alive'] == '0' and float(house['eliminated_s']) <= minute * 60:
        return 0
    return None  # A surviving early winner was not observed at this checkpoint.


def combat_report(directories, allowed=None, label='Combat'):
    """Exact ground dispatches when enabled; checkpoints include defeated houses as zero."""
    games = {r['id']: r for r in load(directories, 'games.csv', ('id',)) if not r['error']}
    houses = [r for r in load(directories, 'houses.csv', ('id', 'house'))
              if r['id'] in games and (allowed is None or (r['id'], r['house']) in allowed)]
    samples = checkpoint_samples(directories)
    groups = defaultdict(list)
    for house in houses:
        groups[(house['difficulty'], house['spice_level'])].append(house)
    groups[('all', 'all')] = houses
    print(f'{label}: first attack is a nonempty ground_hunt dispatch, not a readiness check or sortie duration.')
    print('| Difficulty | Spice | Attacking/measured | First attack mean min (attackers) | Attacks/10 alive-min | Army10 mean (n) | Army20 mean (n) | Rocket10 mean (n) | Rocket20 mean (n) | Kill-value mean |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), rows in sorted(groups.items()):
        measured = [r for r in rows if r.get('attack_telemetry') == '1']
        first = [float(r['first_attack_minute']) for r in measured if r.get('first_attack_minute', '') != '']
        minutes = sum(min(float(games[r['id']]['minutes']), float(r['eliminated_s']) / 60)
                      if r['alive'] == '0' else float(games[r['id']]['minutes']) for r in measured)
        frequency = sum(int(r['attack_count']) for r in measured) * 10 / minutes if minutes else None
        checkpoints = []
        for field in ('army_value', 'rocket'):
            for minute in (10, 20):
                values = []
                for house in rows:
                    value = checkpoint_value(house, samples, minute, field)
                    if value is not None:
                        values.append(value)
                checkpoints.append(f'{statistics.mean(values):.1f} ({len(values)})' if values else 'unmeasured')
        first_mean = f'{statistics.mean(first):.2f}' if first else 'unmeasured'
        rate = f'{frequency:.3f}' if frequency is not None else 'unmeasured'
        kills = f'{statistics.mean(int(r["kill_value"]) for r in rows):.1f}' if rows else 'unmeasured'
        print(f'| {difficulty} | {spice} | {len(first)}/{len(measured)} | {first_mean} | {rate} | '
              + ' | '.join(checkpoints) + f' | {kills} |')

def storage_report(directories, allowed=None, label='Storage'):
    rows = [row for row in load(directories, 'houses.csv', ('id', 'house'))
            if (allowed is None or (row['id'], row['house']) in allowed)
            and row.get('storage_overflow_cycles', '') != '']
    if not rows:
        print(f'{label}: full/overflow cycle accounting unmeasured')
        return
    print(f'{label}: {len(rows)} measured houses; '
          f'full-storage {sum(int(row["storage_full_cycles"]) for row in rows) / CYCLES_PER_MINUTE:.3f} house-min; '
          f'income-overflow {sum(int(row["storage_overflow_cycles"]) for row in rows) / CYCLES_PER_MINUTE:.3f} house-min; '
          f'{sum(int(row["storage_income_lost"]) for row in rows)} credits lost '
          '(capacity-loss ticks excluded)')


def measured_air_metric(row, field):
    if field in AIR_COUNT_FIELDS and row.get('air_kill_telemetry') != 'complete':
        return False
    return field not in TROOPER_ENEMY_AIR_FIELDS.values() or row.get('air_kill_attribution_complete') == '1'


def support_cell(rows, field):
    values = [int(row[field]) for row in rows if row.get(field, '') != '' and measured_air_metric(row, field)]
    if not values:
        return 'unmeasured'
    cell = f'{statistics.mean(values):.2f} ({len(values)}/{len(rows)})'
    if field == 'wor_built':
        built = sum(value > 0 for value in values)
        cell += f'; built>0 {built}/{len(values)} ({100 * built / len(values):.1f}%)'
    if field in ('wor_built', 'trooper_built') or field in TROOPER_ENEMY_AIR_FIELDS.values():
        cell += f'; total {sum(values)}'
    return cell


def trooper_report(directories):
    games = [row for row in load(directories, 'games.csv', ('id',)) if not row['error']]
    completed = {row['id'] for row in games}
    groups = defaultdict(list)
    for row in load(directories, 'houses.csv', ('id', 'house')):
        if row['id'] in completed:
            groups[(row['difficulty'], row['spice_level'])].append(row)
    print('Troopers/air: final live counts and cumulative engine ledgers; means show measured house counts. '
          'WOR built>0 is the percentage of measured house histories that ever completed a WOR. '
          'Trooper built totals are engine-completed/delivered singles after squad expansion, not strictly '
          'WOR-trained; queued counts single-equivalents.')
    tables = (
        (('wor', 'WOR live'), ('wor_built', 'WOR built'), ('trooper_live', 'Trooper live'),
         ('trooper_queued', 'Trooper queued'), ('trooper_built', 'Trooper built')),
        (('ornithopter_lost', 'Own Ornithopter losses'), ('elite_ornithopter_lost', 'Own elite losses'),
         ('enemy_ornithopter_kills', 'Enemy Ornithopter kills'), ('enemy_elite_ornithopter_kills', 'Enemy elite kills')),
        (('trooper_enemy_ornithopter_kills', 'Trooper enemy Ornithopter kills'),
         ('trooper_enemy_elite_ornithopter_kills', 'Trooper enemy elite kills')),
    )
    for table in tables:
        print()
        print('| Difficulty | Spice | ' + ' | '.join(label for _, label in table) + ' |')
        print('|---|---|' + '---|' * len(table))
        for (difficulty, spice), rows in sorted(groups.items()):
            cells = [support_cell(rows, field) for field, _ in table]
            print(f'| {difficulty} | {spice} | ' + ' | '.join(cells) + ' |')
    attributed = [row for row in games if row.get('air_kill_attribution') == 'complete']
    unknown = [row for row in games if row.get('air_kill_telemetry') == 'complete'
               and row.get('air_kill_unknown_attribution', '') != '']
    print(f'Air source attribution: {len(attributed)}/{len(games)} complete games; '
          f'{sum(int(row["air_kill_unknown_attribution"]) for row in unknown)} observed unknown-source callbacks '
          f'in {len(unknown)}/{len(games)} complete streams (count games once, not once per house).')
    print('Enemy kills require complete lethal_damage callbacks and opposing teams; raw *_killed house ledgers '
          'include friendly splash. Trooper kills require natural attacker_item and firing-house attribution, '
          'not reward_item or the shooter at impact. Any unknown air source censors exact Trooper totals '
          'game-wide; air_kill_unknown_attribution is a game-wide running count, not additive across houses. '
          'Missing history stays unmeasured.')


def trooper_pair_keys(previous, current, old, new, styles=('trooper-support-off', 'trooper-support-on')):
    """Match OFF/ON source and rules; native equivalence needs cluster-parity receipts."""
    matched, rejected = [], defaultdict(int)
    fingerprints = ('source_commit', 'source_fingerprint', 'map_hash',
                    'options_hash', 'content_hash', 'object_data_hash')
    settings = tuple(field for field in META if field not in ('id', 'variant', 'style')) + ('sides',)
    for key in sorted(old.keys() & new.keys()):
        before, after = previous.get(key[0]), current.get(key[0])
        reason = ''
        if not before or not after or before.get('error') or after.get('error'):
            reason = 'incomplete_game'
        elif observer_pair_error(before, after):
            reason = observer_pair_error(before, after)
        elif styles is not None and (before.get('style'), after.get('style')) != styles:
            reason = 'not_trooper_support_OFF_ON'
        elif any(before.get(field, '') == '' or after.get(field, '') == ''
                 for field in fingerprints + ('probe_sha256',)):
            reason = 'missing_provenance'
        elif any(before[field] != after[field] for field in fingerprints):
            reason = 'different_source_map_or_options'
        elif before.get('source_fingerprint_error') or after.get('source_fingerprint_error'):
            reason = 'source_fingerprint_error'
        elif any(before.get(field, '') != after.get(field, '') for field in settings):
            reason = 'different_game_settings'
        elif any(old[key].get(field, '') != new[key].get(field, '') for field in ('team', 'difficulty')):
            reason = 'different_house_settings'
        if reason:
            rejected[reason] += 1
        else:
            matched.append(key)
    return matched, dict(rejected)


def clustered_guardrail(pairs, direction, complete=True):
    """One observation per game: bootstrap game deltas and exact two-sided sign test."""
    if not pairs:
        return 'unmeasured', 'unmeasured'
    deltas = [after - before for before, after in pairs]
    low, high = match_score.bootstrap(deltas, 9101)
    better = sum(direction * delta > 0 for delta in deltas)
    worse = sum(direction * delta < 0 for delta in deltas)
    nonzero = better + worse
    pvalue = min(1.0, 2 * sum(math.comb(nonzero, k) for k in range(min(better, worse) + 1))
                 / 2 ** nonzero) if nonzero else 1.0
    mean = statistics.mean(deltas)
    state = 'unmeasured' if not complete else 'regression supported' if worse > better and pvalue <= 0.05 \
        else 'inconclusive' if direction * mean < 0 else 'no supported regression'
    point = 'decrease' if mean < 0 else 'increase' if mean > 0 else 'unchanged'
    return (f'game-mean delta {mean:+.3f}, 95% [{low:+.3f}, {high:+.3f}]; '
            f'sign better/worse/tied {better}/{worse}/{len(pairs)-nonzero}, p2={pvalue:.4f}, '
            f'{len(pairs)} games; point estimate {point}; {state}'), state


def paired_metric(keys, old, new, field, direction=None, telemetry=False, interval=False):
    values, by_game = [], defaultdict(list)
    for key in keys:
        rows = (old[key], new[key])
        if any(row.get(field, '') == '' or not measured_air_metric(row, field) for row in rows):
            continue
        if telemetry and any(row.get('air_kill_telemetry') != 'complete'
                             or row.get('attack_telemetry') != '1' for row in rows):
            continue
        pair = tuple(float(row[field]) for row in rows)
        values.append(pair)
        by_game[key[0]].append(pair)
    if not values:
        return 'unmeasured', 'unmeasured'
    before, after = (statistics.mean(pair[side] for pair in values) for side in (0, 1))
    cell = f'{before:.2f} -> {after:.2f} ({after-before:+.2f}; {len(values)}/{len(keys)} pairs)'
    if field == 'wor_built':
        cell += '; built>0 ' + ' -> '.join(
            f'{sum(pair[side] > 0 for pair in values)}/{len(values)} '
            f'({100 * sum(pair[side] > 0 for pair in values) / len(values):.1f}%)' for side in (0, 1))
    if field in ('wor_built', 'trooper_built') or field in TROOPER_ENEMY_AIR_FIELDS.values():
        cell += '; totals ' + ' -> '.join(f'{sum(pair[side] for pair in values):.0f}' for side in (0, 1))
    game_pairs = [tuple(statistics.mean(pair[side] for pair in group) for side in (0, 1))
                  for group in by_game.values()]
    if direction is None:
        if interval:
            deltas = [after - before for before, after in game_pairs]
            low, high = match_score.bootstrap(deltas, 9101)
            cell += f'; game-mean delta {statistics.mean(deltas):+.3f}, 95% [{low:+.3f}, {high:+.3f}]; {len(deltas)} games'
        return cell, 'unmeasured'
    evidence, state = clustered_guardrail(game_pairs, direction, len(values) == len(keys))
    return cell + '; ' + evidence, state


def paired_detector(keys, old, new, detector):
    fields = (detector, 'eligible_' + detector)
    if not keys or any(row.get(field, '') == '' for key in keys
                       for row in (old[key], new[key]) for field in fields):
        return 'unmeasured', 'unmeasured'
    counts = [tuple(sum(int(source[key][field]) for key in keys) for field in fields) for source in (old, new)]
    by_game = defaultdict(list)
    for key in keys:
        by_game[key[0]].append(key)
    pairs, complete = [], True
    for group in by_game.values():
        game_counts = [tuple(sum(int(source[key][field]) for key in group) for field in fields)
                       for source in (old, new)]
        if all(exposed for _, exposed in game_counts):
            pairs.append(tuple(hit / exposed for hit, exposed in game_counts))
        elif any(hit or exposed for hit, exposed in game_counts):
            complete = False  # Exposure vanished in one arm; never call that an improvement.
    evidence, state = clustered_guardrail(pairs, -1, complete)
    return f'{counts[0][0]}/{counts[0][1]} -> {counts[1][0]}/{counts[1][1]}; {evidence}', state


def trooper_comparison(control, candidate):
    games = [{row['id']: row for row in load([directory], 'games.csv', ('id',))}
             for directory in (control, candidate)]
    houses = [{(row['id'], row['house']): row for row in load([directory], 'houses.csv', ('id', 'house'))}
              for directory in (control, candidate)]
    keys, rejected = trooper_pair_keys(*games, *houses)
    print(f'Trooper support same-source OFF -> ON: {len(keys)} source-matched house pairs; '
          f'excluded {sum(rejected.values())} ({json.dumps(rejected, sort_keys=True)}). '
          'Source/map/options/content/object-data fingerprints must match; probe hashes remain receipts. '
          'AI config hashes may differ; native equivalence requires external cluster-parity receipts.')
    groups = defaultdict(list, {(level, 'all'): [] for level in ('hard', 'brutal')})
    for key in keys:
        house = houses[0][key]
        if house['difficulty'] in ('hard', 'brutal'):
            groups[(house['difficulty'], house['spice_level'])].append(key)
            groups[(house['difficulty'], 'all')].append(key)
    print('Guardrails: point-estimate decreases in dispatch/kill value or increases in detector rates are '
          'not automatic failures. Game means/rates are equally weighted independent clusters; 95% bootstrap '
          'intervals describe magnitude and exact two-sided paired game sign p<=0.05 identifies consistently '
          'adverse regression. Houses within a game are not independent. No supported regression is not '
          'proof of equivalence; unmeasured is not a passing gate. Dispatches remain nonempty ground_hunt '
          'events and require a complete callback capture. Outcome scores are a separate acceptance input.')
    print()
    print('| Difficulty | Spice | Pairs | economy_stall | bankrupt_queue_empty | missing_starport | Dispatches mean | Kill-value mean | Guardrails |')
    print('|---|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), paired in sorted(groups.items()):
        cells = [paired_detector(paired, *houses, detector) for detector in (
            'economy_stall', 'bankrupt_queue_empty', 'missing_starport')]
        cells += [paired_metric(paired, *houses, 'attack_count', direction=1, telemetry=True),
                  paired_metric(paired, *houses, 'kill_value', direction=1)]
        states = [state for _, state in cells]
        verdict = next((state for state in ('regression supported', 'unmeasured', 'inconclusive') if state in states),
                       'no supported regression')
        print(f'| {difficulty} | {spice} | {len(paired)} | ' + ' | '.join(cell for cell, _ in cells) + f' | {verdict} |')
    tables = (
        (('wor', 'WOR live'), ('wor_built', 'WOR built'), ('trooper_live', 'Trooper live'),
         ('trooper_queued', 'Trooper queued'), ('trooper_built', 'Trooper built')),
        (('ornithopter_lost', 'Own Ornithopter losses'), ('elite_ornithopter_lost', 'Own elite losses'),
         ('enemy_ornithopter_kills', 'Enemy Ornithopter kills'), ('enemy_elite_ornithopter_kills', 'Enemy elite kills')),
        (('trooper_enemy_ornithopter_kills', 'Trooper enemy Ornithopter kills'),
         ('trooper_enemy_elite_ornithopter_kills', 'Trooper enemy elite kills')),
    )
    print('Support and AA: paired final-house means OFF -> ON (delta; measured/verified pairs). '
          'Own losses use House original-owner loss counters; enemy kills use opposing-team lethal callbacks, '
          'never enemy total losses. Live counts are observed at each game end, not equal-time checkpoints.')
    print('WOR built>0 gives OFF/ON completed-WOR house percentages; completed/delivered Trooper and exact '
          'Trooper-AA totals accompany means. Unknown source callbacks censor exact Trooper totals; '
          'the raw per-source air-kills.csv rows and game-wide unknown counts remain available.')
    for table in tables:
        print()
        print('| Difficulty | Spice | ' + ' | '.join(label for _, label in table) + ' |')
        print('|---|---|' + '---|' * len(table))
        for (difficulty, spice), paired in sorted(groups.items()):
            cells = [paired_metric(paired, *houses, field)[0] for field, _ in table]
            print(f'| {difficulty} | {spice} | ' + ' | '.join(cells) + ' |')




def rocket_comparison(control, candidate, allowed=None):
    valid = [{r['id'] for r in load([directory], 'games.csv', ('id',)) if not r['error']}
             for directory in (control, candidate)]
    houses = [{(r['id'], r['house']): r for r in load([directory], 'houses.csv', ('id', 'house'))
               if r['id'] in games} for directory, games in zip((control, candidate), valid)]
    samples = [checkpoint_samples([directory]) for directory in (control, candidate)]
    shared = sorted(houses[0].keys() & houses[1].keys())
    if allowed is not None:
        shared = [key for key in shared if key in allowed]
    groups = defaultdict(list)
    for key in shared:
        house = houses[0][key]
        groups[(house['difficulty'], house['spice_level'])].append(key)
    groups[('all', 'all')] = shared
    print('Rocket checkpoints: eliminated houses count as zero; surviving early winners are censored. '
          'Joint-alive pairs require actual alive samples in both arms.')
    print('| Difficulty | Spice | Minute | Observable control -> candidate (delta; pairs) | Joint-alive control -> candidate (delta; pairs) | Censored control/candidate |')
    print('|---|---|---|---|---|---|')

    def means(pairs):
        if not pairs:
            return 'unmeasured'
        old = statistics.mean(pair[0] for pair in pairs)
        new = statistics.mean(pair[1] for pair in pairs)
        return f'{old:.2f} -> {new:.2f} ({new-old:+.2f}; {len(pairs)})'

    for (difficulty, spice), keys in sorted(groups.items()):
        for minute in (10, 20):
            observed, alive = [], []
            censored = [0, 0]
            for key in keys:
                values = [checkpoint_value(houses[side][key], samples[side], minute, 'rocket')
                          for side in (0, 1)]
                for side in (0, 1):
                    censored[side] += values[side] is None
                if any(value is None for value in values):
                    continue
                observed.append(values)
                points = [samples[side][key].get(minute * CYCLES_PER_MINUTE) for side in (0, 1)]
                if all(point and point['alive'] == '1' for point in points):
                    alive.append(values)
            print(f'| {difficulty} | {spice} | {minute} | {means(observed)} | {means(alive)} | '
                  f'{censored[0]}/{censored[1]} |')


def metric_mean(rows, field):
    values = [float(row[field]) for row in rows if row.get(field, '') != '']
    return f'{statistics.mean(values):.2f} ({len(values)}/{len(rows)})' if values else 'unmeasured'


def tech_report(directories):
    houses = load(directories, 'tech-houses.csv', ('id', 'house'))
    if not houses:
        print('Tech savings: replacement composition, categorized accepted spending and feasible 4m candidates unmeasured')
        return
    groups = defaultdict(list)
    for row in houses:
        groups[(row['difficulty'], row['spice_level'])].append(row)
    print('Tech savings: current-owner replacement prices include every live own mobile combat unit '
          '(including inactive/repair units) and queued single-equivalent commitments, never the army cap. '
          'Composition shares normalize the ten unit_mix vehicle slots; infantry quota is separate. '
          'Accepted prices are commitments, NOT paid installments; production/economy/military/other partition '
          'them, while cheap-unit/tech/city are diagnostic overlays and must not be added to that partition.')
    tables = (
        ('Accepted commitments', tuple('accepted_' + name for name in SPEND_CATEGORIES)),
        ('First owned minute (initial bases are zero; missing histories censored)',
         tuple(f'first_owned_{name}_minute' for name in TECH_ITEMS.values())),
        ('First completed minute (not first ownership/capture)',
         tuple(f'first_completed_{name}_minute' for name in TECH_ITEMS.values())),
        ('Upgrade/research and army', ('first_upgrade_order_minute', 'first_upgrade_completed_observed_minute',
         'first_research_order_minute', 'first_research_completed_minute', 'mean_military_unit_cost',
         'mean_committed_unit_cost', 'feasible_plans', 'observed_4m_plans', 'affordable_4m_plans',
         'quoted_diversion_4m_plans')),
    )
    for label, fields in tables:
        print(label)
        print('| Difficulty | Spice | ' + ' | '.join(fields) + ' |')
        print('|---|---|' + '---|' * len(fields))
        for (difficulty, spice), rows in sorted(groups.items()):
            print(f'| {difficulty} | {spice} | ' + ' | '.join(metric_mean(rows, field) for field in fields) + ' |')
    points = load(directories, 'tech-samples.csv', ('id', 'house', 'cycle'))
    print('Observed checkpoints only (no invented zero credits/prices after elimination or early wins).')
    print('| Difficulty | Spice | Minute | Credits | Structures | Replacement army | Mean unit cost | Spice gross | Tax gross |')
    print('|---|---|---|---|---|---|---|---|---|')
    checkpoints = defaultdict(list)
    for row in points:
        if int(row['cycle']) in (10 * CYCLES_PER_MINUTE, 20 * CYCLES_PER_MINUTE):
            checkpoints[(row['difficulty'], row['spice_level'], int(row['cycle']) // CYCLES_PER_MINUTE)].append(row)
    for (difficulty, spice, minute), rows in sorted(checkpoints.items()):
        cells = [metric_mean(rows, field) for field in ('credits', 'structures', 'military_live_value',
                                                       'mean_military_unit_cost', 'spice_gross', 'tax_gross')]
        print(f'| {difficulty} | {spice} | {minute} | ' + ' | '.join(cells) + ' |')
    mix = defaultdict(list)
    for row in load(directories, 'composition.csv', ('id', 'house', 'cycle', 'slot')):
        if int(row['cycle']) in (10 * CYCLES_PER_MINUTE, 20 * CYCLES_PER_MINUTE):
            mix[(row['difficulty'], row['spice_level'], int(row['cycle']) // CYCLES_PER_MINUTE, row['slot'])].append(row)
    print('Composition at observed checkpoints: target_bps is a VALUE share, never a count quota.')
    print('| Difficulty | Spice | Minute | Slot | Live count | Queued singles | Committed value | Value share bps | Target bps | Value-target bps |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for (difficulty, spice, minute, slot), rows in sorted(mix.items()):
        fields = ('live_count', 'queued_count', 'committed_value', 'committed_value_bps', 'target_bps', 'value_target_delta_bps')
        print(f'| {difficulty} | {spice} | {minute} | {slot} | '
              + ' | '.join(metric_mean(rows, field) for field in fields) + ' |')
    paths = defaultdict(list)
    for row in load(directories, 'cheap-paths.csv', ('id', 'house', 'rule', 'composition_basis', 'item')):
        paths[(row['rule'], row['composition_basis'], row['item'])].append(row)
    print('Top cheap-unit accepted paths by certified quoted credits (blank rules/basis are unrecorded, not inferred).')
    print('| Rule | Composition basis | Item | Accepted credits | Orders | Measured house histories |')
    print('|---|---|---|---|---|---|')
    ranked = []
    for key, rows in paths.items():
        measured = [row for row in rows if row['capture_complete'] == '1' and row['accepted_credits'] != '']
        ranked.append((sum(int(row['accepted_credits']) for row in measured), key, measured, len(rows)))
    for credits, (rule, basis, item), measured, count in sorted(ranked, key=lambda entry: -entry[0])[:12]:
        orders = sum(int(row['order_count']) for row in measured)
        rule, basis = rule or 'unrecorded', basis or 'unrecorded'
        credits_cell, orders_cell = (credits, orders) if measured else ('unmeasured', 'unmeasured')
        print(f'| {rule} | {basis} | {item} | {credits_cell} | {orders_cell} | {len(measured)}/{count} |')
    print('A quoted_diversion_4m is a feasible candidate affordable from cash minus unpaid commitments '
          'and working reserve plus the recorded four-minute NET income forecast, but not after subsequent accepted cheap-unit quotes, '
          'with no matching candidate accepted. It is a quote-budget counterfactual, NOT proof of actual paid '
          'cash diversion. Death/early game end, missing fields, another horizon or incomplete captures censor '
          'the window. Repeated build passes/windows overlap and are not independent games. Upgrade completion '
          'is the first sampled level increase (an upper bound); order and research event cycles are exact.')


def tech_comparison(control, candidate):
    games = [{row['id']: row for row in load([directory], 'games.csv', ('id',))}
             for directory in (control, candidate)]
    houses = [{(row['id'], row['house']): row for row in load([directory], 'houses.csv', ('id', 'house'))}
              for directory in (control, candidate)]
    keys, rejected = trooper_pair_keys(*games, *houses, styles=None)
    print(f'Tech/causal comparison: {len(keys)} same-source/map/options/content house pairs; '
          f'excluded {json.dumps(rejected, sort_keys=True)}. OFF/ON arm labels are supplied by the caller; '
          'AI config hashes may differ, native equivalence still needs cluster-parity receipts.')
    groups = defaultdict(list)
    for key in keys:
        row = houses[0][key]
        groups[(row['difficulty'], row['spice_level'])].append(key)
    print('| Difficulty | Spice | Pairs | economy_stall | bankrupt_queue_empty | missing_starport | Dispatches | Kill value |')
    print('|---|---|---|---|---|---|---|---|')
    for (difficulty, spice), paired in sorted(groups.items()):
        cells = [paired_detector(paired, *houses, detector)[0]
                 for detector in ('economy_stall', 'bankrupt_queue_empty', 'missing_starport')]
        cells += [paired_metric(paired, *houses, 'attack_count', direction=1, telemetry=True)[0],
                  paired_metric(paired, *houses, 'kill_value', direction=1)[0]]
        print(f'| {difficulty} | {spice} | {len(paired)} | ' + ' | '.join(cells) + ' |')
    tech = [{(row['id'], row['house']): row for row in load([directory], 'tech-houses.csv', ('id', 'house'))}
            for directory in (control, candidate)]
    present = [key for key in keys if all(key in rows for rows in tech)]
    print(f'Tech metrics: {len(present)}/{len(keys)} matched house histories; blank/censored is never passing.')
    for field in TECH_HOUSE_VALUES:
        if field in ('order_capture', 'spending_complete'):
            continue
        print(field + ': ' + paired_metric(present, *tech, field, interval=True)[0])
    print('Guardrail intervals/sign tests use equally weighted independent game clusters, not houses. '
          'Tech timings average observed pairs only; changing completion/exposure counts is not an improvement. '
          'No supported regression is not equivalence, and two seeds alone cannot establish a safe default ON.')


def compare(control, candidate):
    games = [{row['id']: row for row in load([directory], 'games.csv', ('id',))}
             for directory in (control, candidate)]
    require_matching_observers(*games)
    if any(game.get('candidate_house', '') != '' for arm in games for game in arm.values()):
        sample_comparison(control, candidate)
        return
    old = {(row['id'], row['house']): row for row in load([control], 'houses.csv', ('id', 'house'))}
    new = {(row['id'], row['house']): row for row in load([candidate], 'houses.csv', ('id', 'house'))}
    common = sorted(old.keys() & new.keys())
    print(f'Paired houses {len(common)} (control-only {len(old.keys() - new.keys())}, '
          f'candidate-only {len(new.keys() - old.keys())}); same game ids and houses only')
    print('| Detector | Control hit/exposed | Candidate hit/exposed |')
    print('|---|---|---|')
    for name in DETECTORS:
        rates = []
        for source in (old, new):
            rates.append(f'{sum(int(source[key].get(name, 0)) for key in common)}/'
                         f'{sum(int(source[key].get("eligible_" + name, 0)) for key in common)}')
        print(f'| {name} | ' + ' | '.join(rates) + ' |')
    for level in CLASSES:
        by_game = defaultdict(list)
        for key in common:
            if old[key]['difficulty'] == level:
                by_game[key[0]].append(key)
        # Houses in one match are not independent observations: bootstrap match means.
        pairs = [(statistics.mean(float(new[k]['score']) for k in keys),
                  statistics.mean(float(old[k]['score']) for k in keys),
                  all(new[k]['drawn'] == '1' for k in keys), all(old[k]['drawn'] == '1' for k in keys))
                 for keys in by_game.values()]
        if pairs:
            result = match_score.compare(pairs, 9101)
            print(f'{level}: mean score {result.control:.3f} -> {result.candidate:.3f}, '
                  f'paired delta {result.difference:+.3f} [{result.low:+.3f}, {result.high:+.3f}], '
                  f'{result.verdict}; wins {sum(old[k]["won"] == "1" for k in common if old[k]["difficulty"] == level)} -> '
                  f'{sum(new[k]["won"] == "1" for k in common if old[k]["difficulty"] == level)}')
    previous = {row['id']: row for row in load([control], 'games.csv', ('id',)) if not row['error']}
    current = {row['id']: row for row in load([candidate], 'games.csv', ('id',)) if not row['error']}
    keys = previous.keys() & current.keys()
    if keys:
        for label, source in (('Control', previous), ('Candidate', current)):
            print(f'{label}: mean game {statistics.mean(float(source[k]["minutes"]) for k in keys):.2f} min; '
                  f'mean loop {statistics.mean(float(source[k]["loop_ms"]) for k in keys):.1f} ms '
                  '(diagnostic only: mixed host/load, not a performance gate); '
                  f'conquest wins {sum(int(source[k]["winner_team"]) > 0 for k in keys)}, '
                  f'population wins {sum(source[k].get("win_condition") == "population" for k in keys)}')
    combat_report([control], set(common), 'Control combat')
    combat_report([candidate], set(common), 'Candidate combat')
    storage_report([control], set(common), 'Control storage')
    storage_report([candidate], set(common), 'Candidate storage')
    rocket_comparison(control, candidate, set(common))
    trooper_comparison(control, candidate)
    tech_comparison(control, candidate)


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--results', type=Path)
parser.add_argument('--export-maps', type=Path, metavar='DIR',
                    help='Retain exact generated INI/settings in DIR (relative to results); '
                         'export-maps.csv carries chunked base64 bytes for cluster collection. '
                         'Decode each id/artifact by numeric chunk; verify bytes and sha256.')
parser.add_argument('--build-dir', default=str(root / 'build-int'))
parser.add_argument('--prebuilt-probe', type=Path, help='Use a frozen probe binary without building or leasing the checkout')
parser.add_argument('--scratch')
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--capture-mib', type=int, default=256,
                    help='Ordinary diagnostic telemetry allowance (256-4096 MiB); shipped log cap is unchanged')
parser.add_argument('--sizes', nargs='+', type=int, choices=(96, 128, 160), default=[96, 128, 160])
parser.add_argument('--spice', nargs='+', choices=tuple(SPICE), default=list(SPICE))
parser.add_argument('--formats', nargs='+', choices=('1v1', '2v2', 'ffa4', 'ffa6'), default=['1v1', '2v2', 'ffa4'])
parser.add_argument('--difficulties', nargs='+', choices=('medium', 'hard', 'brutal'), default=['medium', 'hard', 'brutal'])
parser.add_argument('--seeds', nargs='+', type=int, default=[9101, 148680876])
parser.add_argument('--minutes', type=int, default=120,
                    help='Game-minute cap (default: DuneCity 120, vanilla/Dune2R 90); shorter caps apply to every mod')
parser.add_argument('--mod', choices=('dunecity', 'Dune2R', 'vanilla', 'both'), default='dunecity',
                    help='Content family; --mode sample accepts vanilla, dunecity or both')
parser.add_argument('--tech', type=int, choices=range(1, 10), default=9,
                    help='Initial house technology; non-9 runs have distinct ids and recorded initial_tech metadata')
parser.add_argument('--base', choices=('cy', 'small', 'medium', 'large', 'fortress'), default='medium')
parser.add_argument('--army', type=int, choices=range(7), default=2)
parser.add_argument('--city', type=int, choices=range(6), default=0)
parser.add_argument('--mode', choices=('matrix', 'ladder', 'sample'), default='matrix',
                    help='sample: balanced seeded factor draws, each played from two candidate seats')
parser.add_argument('--cases', type=int, default=48, help='sample: cases per mod before the two seat assignments')
parser.add_argument('--sample-seed', type=int, default=9101, help='sample: reproducible factor draws and 32-bit game seeds')
parser.add_argument('--case-manifest', type=Path,
                    help='sample only: explicit schema-v1 case JSON for common-seed screens; '
                         'format/size/spice/tech/composition are recorded in the manifest and start receipts')
parser.add_argument('--sample-enemy', choices=('easy', 'medium', 'hard', 'brutal'),
                    help='sample: fix every non-candidate controller to this tier; factor/map draws stay unchanged')
parser.add_argument('--sample-formats', nargs='+', choices=('1v1', '2v2', 'ffa4', 'ffa6'), default=None,
                    help='sample: override only the balanced format deck (default: all four formats)')
parser.add_argument('--shard', type=shard, help='K/N: play every N-th scheduled game starting with the K-th')
parser.add_argument('--variant', default='baseline')
parser.add_argument('--game-options', type=Path)
styles = parser.add_mutually_exclusive_group()
styles.add_argument('--style', help='All-QuantBot overlay from tests/ai/play-styles/ (not candidate-only)')
styles.add_argument('--candidate-style',
                    help='sample only: play-style INI applied only to the candidate seat; opponents keep native config')
styles.add_argument('--seat-styles', type=seat_styles_argument, metavar='control,A,B,C',
                    help='Constant per-seat INIs, rotated through every seat; native control fills spare seats')
parser.add_argument('--observer-off', action='store_true',
                    help='Disable BALANCE_DUNECITY_AUDIT for outcome/digest gates; audit metrics are unmeasured')
parser.add_argument('--attack-events', action='store_true',
                    help='Enable existing AI telemetry, retain exact ground dispatches in waves.csv, and report passivity metrics')
parser.add_argument('--keep-telemetry', action='store_true',
                    help='For selected successful games, enable telemetry and copy raw run.log, summary.json '
                         'and events.jsonl to results/diagnostics/<gamehash>/ before normal scratch cleanup; '
                         'default off (target games with --seeds/--sizes/--formats/--difficulties)')
parser.add_argument('--list', action='store_true')
parser.add_argument('--report', nargs='+', type=Path, dest='report_dirs')
parser.add_argument('--compare', nargs=2, type=Path, metavar=('CONTROL', 'CANDIDATE'))
args = parser.parse_args()
if not 1 <= args.minutes <= 120 or any(not 0 <= seed <= 0xffffffff for seed in args.seeds):
    parser.error('Use 1-120 game minutes and unsigned 32-bit seeds')
if not 256 <= args.capture_mib <= 4096:
    parser.error('Use 256-4096 MiB of capture space')
if args.cases < 1 or not 0 <= args.sample_seed <= 0xffffffff:
    parser.error('Use positive --cases and an unsigned 32-bit --sample-seed')
if args.mod == 'both' and args.mode != 'sample' or args.mode == 'sample' and args.mod == 'Dune2R':
    parser.error('--mode sample accepts --mod vanilla|dunecity|both; both is sample-only')
if args.candidate_style and args.mode != 'sample':
    parser.error('--candidate-style requires --mode sample')
if args.case_manifest and (args.mode != 'sample' or args.mod == 'both'):
    parser.error('--case-manifest requires --mode sample and one explicit --mod')
if args.compare:
    compare(*args.compare)
elif args.report_dirs:
    report(args.report_dirs)
elif args.list:
    games = schedule(args)
    for game in games:
        candidate = (f'candidate_house={game["candidate_house"]} seat={game["candidate_seat"]} '
                     f'research_required={int(game["require_research"])}') if args.mode == 'sample' else ''
        print(game['id'], game['skirmish'], candidate)
    limits = '/'.join(str(limit) for limit in sorted({game['limit'] for game in games}))
    print(f'{len(games)} games, {sum(len(game["sides"]) for game in games)} houses, {limits} game-minute caps')
    if args.mode == 'sample':
        sample_coverage(games)
elif not args.results:
    parser.error('--results is required to play games')
else:
    run(args)