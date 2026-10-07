#!/usr/bin/env python3
"""Import DuneCity AI JSONL (or old zoning logs) into a queryable local SQLite file."""
import argparse
from collections import Counter, defaultdict
import statistics
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import sys

SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
 session TEXT NOT NULL, seq INTEGER NOT NULL, cycle INTEGER, house INTEGER,
 player INTEGER, event TEXT NOT NULL, data TEXT NOT NULL,
 schema_version INTEGER NOT NULL, source TEXT NOT NULL, record TEXT,
 PRIMARY KEY(session, seq));
CREATE INDEX IF NOT EXISTS events_kind ON events(event, session, house, cycle);
CREATE VIEW IF NOT EXISTS performance_windows AS
 SELECT session,seq,cycle,json_extract(data,'$.start_cycle') AS start_cycle,
 json_extract(data,'$.elapsed_us')/1000000.0 AS wall_seconds,
 json_extract(data,'$.worst_frame_cycle') AS worst_frame_cycle,
 json_extract(data,'$.worst_frame_us')/1000.0 AS worst_frame_ms,
 json_extract(data,'$.worst_frame') AS worst_frame,
 json_extract(data,'$.dropped_samples') AS dropped_samples
 FROM events WHERE event='performance_window';
CREATE VIEW IF NOT EXISTS performance_metrics AS
 SELECT e.session,e.seq,e.cycle,json_extract(e.data,'$.start_cycle') AS start_cycle,
 json_extract(e.data,'$.elapsed_us')/1000000.0 AS wall_seconds,
 json_extract(m.value,'$.scope') AS scope,json_extract(m.value,'$.house') AS house,
 json_extract(m.value,'$.item') AS item,json_extract(m.value,'$.unit') AS unit,
 json_extract(m.value,'$.count') AS samples,json_extract(m.value,'$.sum') AS total,
 json_extract(m.value,'$.max') AS maximum,json_extract(m.value,'$.max_cycle') AS max_cycle,
 json_extract(m.value,'$.over_33ms') AS over_33ms,
 json_extract(m.value,'$.over_100ms') AS over_100ms,
 json_extract(m.value,'$.over_250ms') AS over_250ms
 FROM events e,json_each(e.data,'$.metrics') m WHERE e.event='performance_window';
CREATE VIEW IF NOT EXISTS economy_samples AS
 SELECT session,seq,cycle,house,player,
 json_extract(data,'$.state.credits') AS credits,
 json_extract(data,'$.economy_totals.spice_refined') AS spice_refined,
 json_extract(data,'$.economy_totals.city_gross') AS city_gross,
 json_extract(data,'$.economy_totals.spent_total') AS spent_total,
 json_extract(data,'$.economy_totals.storage_lost') AS storage_lost,
 json_extract(data,'$.economy_totals.power_spent') AS power_spent,
 json_extract(data,'$.economy_totals.refunded') AS refunded
 FROM events WHERE event='state_snapshot';
CREATE VIEW IF NOT EXISTS production AS
 SELECT session, seq, cycle, house, player,
 json_extract(data,'$.builder') AS builder,
 json_extract(data,'$.item') AS item,
 json_extract(data,'$.item_name') AS item_name,
 json_extract(data,'$.accepted') AS accepted,
 json_extract(data,'$.state.credits') AS credits,
 json_extract(data,'$.state.res_demand') AS res_demand,
 json_extract(data,'$.state.com_demand') AS com_demand,
 json_extract(data,'$.state.ind_demand') AS ind_demand,
 json_extract(data,'$.state_id') AS state_id,
 json_extract(data,'$.zone_decision') AS zone_decision
 FROM events WHERE event='production_order';
CREATE VIEW IF NOT EXISTS capital_plans AS
 SELECT session,seq AS plan,cycle,house,player,
 json_extract(data,'$.mode') AS mode,json_extract(data,'$.campaign') AS campaign,
 json_extract(data,'$.spendable') AS spendable,json_extract(data,'$.committed_cost') AS committed_cost,
 json_extract(data,'$.forecast_net_income') AS forecast_income,json_extract(data,'$.forecast_funding') AS forecast_funding,
 json_extract(data,'$.military_value') AS military_value,json_extract(data,'$.military_target') AS military_target,
 json_extract(data,'$.workers') AS workers,json_extract(data,'$.worker_target') AS worker_target,
 json_extract(data,'$.spice_share') AS spice_share,json_extract(data,'$.defending') AS defending,
 json_extract(data,'$.existing_military_capacity') AS existing_capacity,
 json_extract(data,'$.construction_capacity_cost') AS construction_capacity,
 json_extract(data,'$.active_production_burn_per_minute') AS active_production_burn,
 json_extract(data,'$.net_burn_per_minute') AS net_burn,
 json_extract(data,'$.cash_runway_seconds') AS runway_seconds,
 json_extract(data,'$.projected_cash') AS projected_cash,
 json_extract(data,'$.funds_parallel_production') AS funds_parallel_production,
 json_extract(data,'$.carryall_target') AS carryall_target,
 json_extract(data,'$.pickup_waiting') AS pickup_waiting,
 json_extract(data,'$.repair_target') AS repair_target,
 json_extract(data,'$.repair_waiting') AS repair_waiting,
 json_extract(data,'$.refinery_capacity_target') AS refinery_target,
 json_extract(data,'$.refinery_waiting') AS refinery_waiting,
 json_extract(data,'$.refinery_field_returners_blocked') AS refinery_field_returners_blocked,
 json_extract(data,'$.refinery_unbooked_bays') AS refinery_unbooked_bays,
 json_extract(data,'$.walking_trip_cycles') AS walking_trip_cycles,
 json_extract(data,'$.trip_cycles') AS trip_cycles,
 json_extract(data,'$.starport_market_available') AS starport_market_available,
 json_extract(data,'$.funded_factory_opening') AS funded_factory_opening,
 json_extract(data,'$.funded_city_production') AS funded_city_production,
 json_extract(data,'$.next_heavy_runway') AS next_heavy_runway,
 json_extract(data,'$.city_growth_protected') AS city_growth_protected,
 json_extract(data,'$.city_growth_yards_busy') AS city_growth_yards_busy,
 json_extract(data,'$.city_growth_dedicated_yard') AS city_growth_dedicated_yard,
 json_extract(data,'$.city_growth_builder') AS city_growth_builder,
 json_extract(data,'$.selected') AS selected,json_extract(data,'$.reason') AS reason
 FROM events WHERE event='capital_plan';
CREATE VIEW IF NOT EXISTS capital_candidates AS
 SELECT e.session,e.seq AS plan,e.cycle,e.house,CAST(c.key AS INTEGER) AS candidate,
 CAST(c.key AS INTEGER)=json_extract(e.data,'$.selected') AS selected,
 json_extract(c.value,'$.builder') AS builder,json_extract(c.value,'$.item') AS item,
 json_extract(c.value,'$.kind') AS kind,json_extract(c.value,'$.price') AS price,
 json_extract(c.value,'$.foundation_cost') AS foundation_cost,
 json_extract(c.value,'$.total_cost_with_power') AS total_cost,
 json_extract(c.value,'$.proceeds_or_military_value') AS proceeds_or_value,
 json_extract(c.value,'$.additional_funded_capacity') AS added_capacity,
 json_extract(c.value,'$.score') AS score,json_extract(c.value,'$.affordable') AS affordable,
 json_extract(c.value,'$.delay_cycles') AS delay_cycles,json_extract(c.value,'$.reason') AS reason
 FROM events e,json_each(e.data,'$.candidates') c WHERE e.event='capital_plan';
CREATE VIEW IF NOT EXISTS capital_orders AS
 SELECT session,seq,cycle,house,json_extract(data,'$.capital_plan') AS plan,
 json_extract(data,'$.builder') AS builder,json_extract(data,'$.item') AS item,
 json_extract(data,'$.item_name') AS item_name,json_extract(data,'$.quoted_price') AS cost,
 json_extract(data,'$.rule') AS rule,'production' AS kind
 FROM events WHERE event='production_order' AND json_extract(data,'$.accepted')=1
 AND json_extract(data,'$.capital_plan')>0
 UNION ALL
 SELECT session,seq,cycle,house,json_extract(data,'$.capital_plan'),json_extract(data,'$.builder'),
 NULL,'Upgrade',json_extract(data,'$.price'),json_extract(data,'$.reason'),'upgrade'
 FROM events WHERE event='capital_upgrade' AND json_extract(data,'$.accepted')=1
 UNION ALL
 SELECT session,seq,cycle,house,json_extract(data,'$.capital_plan'),json_extract(data,'$.builder'),
 23,'Road batch',json_extract(data,'$.cost'),'road_maintenance','roads'
 FROM events WHERE event='capital_road_batch';
CREATE VIEW IF NOT EXISTS capital_outcomes AS
 SELECT session,seq,cycle,house,json_extract(data,'$.plan') AS plan,
 json_extract(data,'$.priority_ordered') AS priority_ordered,
 json_extract(data,'$.priority_pending') AS priority_pending,
 json_extract(data,'$.ordered_cost') AS ordered_cost,
 json_extract(data,'$.remaining_planning_cash') AS remaining_cash
 FROM events WHERE event='capital_outcome';
CREATE VIEW IF NOT EXISTS zone_candidates AS
 SELECT e.session,e.seq,e.cycle,e.house,e.player,c.key AS item,
 json_extract(c.value,'$.rank') AS rank,
 json_extract(c.value,'$.demand') AS demand,
 json_extract(c.value,'$.normalized_demand') AS normalized_demand,
 json_extract(c.value,'$.count_including_queued') AS count_including_queued
 FROM events e,json_each(e.data,'$.candidates') c WHERE e.event='zone_evaluation';
CREATE VIEW IF NOT EXISTS city_buildings AS
 SELECT e.session,e.seq,e.cycle,e.house,CAST(b.key AS INTEGER) AS object,
 json_extract(b.value,'$.item') AS item,
 json_extract(b.value,'$.x') AS x,
 json_extract(b.value,'$.y') AS y,
 json_extract(b.value,'$.width') AS width,
 json_extract(b.value,'$.height') AS height,
 json_extract(b.value,'$.health') AS health,
 json_extract(b.value,'$.max_health') AS max_health,
 json_extract(b.value,'$.role') AS role,
 json_extract(b.value,'$.level') AS level,
 json_extract(b.value,'$.max_level') AS max_level,
 json_extract(b.value,'$.population') AS population,
 json_extract(b.value,'$.res_supply') AS res_supply,
 json_extract(b.value,'$.com_supply') AS com_supply,
 json_extract(b.value,'$.ind_supply') AS ind_supply,
 json_extract(b.value,'$.land_value') AS land_value,
 json_extract(b.value,'$.population_density') AS population_density,
 json_extract(b.value,'$.pollution') AS pollution,
 json_extract(b.value,'$.crime') AS crime,
 json_extract(b.value,'$.crime_before_police') AS crime_before_police,
 json_extract(b.value,'$.police_coverage') AS police_coverage,
 json_extract(b.value,'$.police_cost_milli') AS police_cost_milli,
 json_extract(b.value,'$.traffic_density') AS traffic_density,
 json_extract(b.value,'$.growth_rate') AS growth_rate,
 json_extract(b.value,'$.police_strength') AS police_strength,
 json_extract(b.value,'$.police_cost') AS police_cost,
 json_extract(b.value,'$.power_nominal') AS power_nominal,
 json_extract(b.value,'$.power_generated') AS power_generated,
 b.value AS data
 FROM events e,json_each(e.data,'$.buildings') b WHERE e.event='city_building_snapshot';
CREATE VIEW IF NOT EXISTS city_growth AS
 SELECT session,seq,cycle,house,event,
 json_extract(data,'$.object') AS object,
 json_extract(data,'$.item') AS item,
 json_extract(data,'$.x') AS x,
 json_extract(data,'$.y') AS y,
 json_extract(data,'$.old_level') AS old_level,
 json_extract(data,'$.new_level') AS new_level,
 json_extract(data,'$.outcome') AS outcome,
 json_extract(data,'$.reason') AS reason,
 json_extract(data,'$.score') AS score,
 json_extract(data,'$.hostile_value_penalty') AS hostile_value_penalty,
 json_extract(data,'$.crime_score_penalty') AS crime_score_penalty,
 json_extract(data,'$.crime') AS crime,
 json_extract(data,'$.crime_before_police') AS crime_before_police,
 json_extract(data,'$.police_coverage') AS police_coverage,
 json_extract(data,'$.pollution') AS pollution,
 json_extract(data,'$.land_value') AS land_value,
 json_extract(data,'$.population_density') AS population_density,
 json_extract(data,'$.powered') AS powered,
 json_extract(data,'$.traffic_result') AS traffic_result,
 json_extract(data,'$.supply_satisfied') AS supply_satisfied,
 json_extract(data,'$.land_value_satisfied') AS land_value_satisfied,
 json_extract(data,'$.road_satisfied') AS road_satisfied,
 json_extract(data,'$.pollution_blocked') AS pollution_blocked,
 json_extract(data,'$.growth_roll_passed') AS growth_roll_passed,
 json_extract(data,'$.at_max_level') AS at_max_level, data
 FROM events WHERE event IN ('city_growth_sample','city_level_changed');

CREATE VIEW IF NOT EXISTS combat_reward_samples AS
 WITH samples AS (
  SELECT e.session,e.seq,e.cycle,CAST(h.key AS INTEGER) AS house,0 AS is_final,u.key AS item,u.value AS stats
   FROM events e,json_each(e.data,'$.house_comparison') h,json_each(h.value,'$.combat_rewards') u
   WHERE e.event='state_snapshot'
  UNION ALL
  SELECT e.session,e.seq,e.cycle,CAST(h.key AS INTEGER),1,u.key,u.value
   FROM events e,json_each(e.data,'$.houses') h,json_each(h.value,'$.combat_rewards') u
   WHERE e.event='game_summary'
 )
 SELECT session,seq,cycle,house,is_final,CAST(item AS INTEGER) AS item,
  json_extract(stats,'$.item_name') AS item_name,
  json_extract(stats,'$.damage_value_milli') AS damage_value_milli,
  json_extract(stats,'$.kill_bonus_milli') AS kill_bonus_milli,
  json_extract(stats,'$.conversion_value_milli') AS conversion_value_milli,
  json_extract(stats,'$.reward_milli') AS reward_milli,
  json_extract(stats,'$.damage_value_milli')/1000.0 AS damage_value,
  json_extract(stats,'$.kill_bonus_milli')/1000.0 AS kill_bonus,
  json_extract(stats,'$.conversion_value_milli')/1000.0 AS conversion_value,
  json_extract(stats,'$.reward_milli')/1000.0 AS reward,
  json_extract(stats,'$.hp_removed_milli')/1000.0 AS hp_removed,
  json_extract(stats,'$.hits') AS hits,
  json_extract(stats,'$.unit_killing_blows') AS unit_killing_blows,
  json_extract(stats,'$.raw_damage') AS raw_damage,
  json_extract(stats,'$.loss_count') AS loss_count,
  json_extract(stats,'$.lost_value') AS lost_value FROM samples;
CREATE VIEW IF NOT EXISTS combat_reward_final AS
 SELECT * FROM combat_reward_samples WHERE is_final=1;
CREATE VIEW IF NOT EXISTS unit_allocation AS
 SELECT e.session,e.seq,e.cycle,e.house,e.player,u.key AS category,
  json_extract(e.data,'$.basis') AS basis,json_extract(e.data,'$.tech_level') AS tech_level,
  json_extract(e.data,'$.performance_confidence_bps') AS performance_confidence_bps,
  json_extract(e.data,'$.performance_exponent_milli') AS performance_exponent_milli,
  json_extract(e.data,'$.total_lost_value') AS total_lost_value,
  json_extract(u.value,'$.available') AS available,
  json_extract(u.value,'$.opening_bps') AS opening_bps,
  json_extract(u.value,'$.target_bps') AS target_bps,
  json_extract(u.value,'$.damage') AS raw_damage,
  json_extract(u.value,'$.reward_milli')/1000.0 AS reward,
  json_extract(u.value,'$.kill_bonus_milli')/1000.0 AS kill_bonus,
  json_extract(u.value,'$.lost_value') AS lost_value,
  json_extract(u.value,'$.score') AS score,
  json_extract(u.value,'$.allocation_weight') AS allocation_weight
 FROM events e,json_each(e.data,'$.mix_inputs') u WHERE e.event='unit_mix';

CREATE VIEW IF NOT EXISTS raid_samples AS
 SELECT e.session,e.seq,e.cycle,e.house,e.player,json_extract(e.data,'$.raid_id') AS raid_id,
 json_extract(e.data,'$.target') AS target,json_extract(e.data,'$.outcome') AS outcome,
 CASE WHEN e.event LIKE 'harvester_strike%' THEN 'main_force_strike' ELSE 'legacy_small_raid' END AS operation,
 e.event IN ('harvester_raid_outcome','harvester_strike_outcome') AS is_final,
 json_extract(e.data,'$.elapsed_cycles') AS elapsed_cycles,
 json_extract(e.data,'$.transit_cycles') AS transit_cycles,
 json_extract(e.data,'$.engagement_cycles') AS engagement_cycles,
 json_extract(e.data,'$.survivors') AS survivors,json_extract(e.data,'$.losses') AS losses,
 json_extract(e.data,'$.captured') AS captured,json_extract(e.data,'$.lost_value') AS lost_value,
 json_extract(e.data,'$.damage_value_milli')/1000.0 AS damage_value,
 json_extract(e.data,'$.kill_bonus_milli')/1000.0 AS kill_bonus,
 json_extract(e.data,'$.reward_milli')/1000.0 AS reward
 FROM events e WHERE e.event IN ('harvester_raid_progress','harvester_raid_outcome',
                                 'harvester_strike_progress','harvester_strike_outcome');
CREATE VIEW IF NOT EXISTS raid_outcomes AS
 SELECT r.*, EXISTS(SELECT 1 FROM events k JOIN events start ON start.session=r.session AND start.seq=r.raid_id
   WHERE k.session=r.session AND k.event='lethal_damage'
   AND json_extract(k.data,'$.object')=r.target AND k.cycle>=start.cycle AND k.cycle<=r.cycle) AS target_death_confirmed
 FROM raid_samples r WHERE is_final=1;
CREATE VIEW IF NOT EXISTS raid_members AS
 SELECT e.session,e.seq AS raid_id,e.cycle,e.house,e.player,CAST(m.key AS INTEGER) AS object,
 CASE WHEN e.event='harvester_strike' THEN 'main_force_strike' ELSE 'legacy_small_raid' END AS operation,
 json_extract(m.value,'$.item') AS item,json_extract(m.value,'$.price') AS price
 FROM events e,json_each(e.data,'$.members') m WHERE e.event IN ('harvester_raid','harvester_strike');
CREATE VIEW IF NOT EXISTS raid_member_results AS
 SELECT e.session,json_extract(e.data,'$.raid_id') AS raid_id,e.house,e.player,CAST(m.key AS INTEGER) AS object,
 CASE WHEN e.event='harvester_strike_outcome' THEN 'main_force_strike' ELSE 'legacy_small_raid' END AS operation,
 json_extract(m.value,'$.lost') AS lost,json_extract(m.value,'$.captured') AS captured,
 json_extract(m.value,'$.reward_milli')/1000.0 AS reward
 FROM events e,json_each(e.data,'$.members') m WHERE e.event IN ('harvester_raid_outcome','harvester_strike_outcome');
CREATE VIEW IF NOT EXISTS heavy_allocation_candidates AS
 SELECT e.session,e.seq,e.cycle,e.house,e.player,CAST(c.key AS INTEGER) AS item,
 json_extract(e.data,'$.selected') AS selected,json_extract(e.data,'$.army_value') AS army_value,
 json_extract(e.data,'$.spendable') AS spendable,json_extract(e.data,'$.horizon_value') AS horizon_value,
 json_extract(c.value,'$.deficit_scaled')/10000.0 AS deficit_value,
 json_extract(c.value,'$.expansion_deficit_scaled')/10000.0 AS expansion_deficit_value,
 json_extract(c.value,'$.committed_value') AS committed_value,json_extract(c.value,'$.target_bps') AS target_bps,
 json_extract(c.value,'$.available') AS available,json_extract(c.value,'$.affordable') AS affordable
 FROM events e,json_each(e.data,'$.candidates') c WHERE e.event='heavy_allocation_decision';

"""


def connect(path):
    db = sqlite3.connect(path)
    db.executescript("DROP VIEW IF EXISTS city_buildings; DROP VIEW IF EXISTS city_growth; "
                     "DROP VIEW IF EXISTS combat_reward_final; DROP VIEW IF EXISTS combat_reward_samples; "
                     "DROP VIEW IF EXISTS unit_allocation; DROP VIEW IF EXISTS heavy_allocation_candidates; "
                     "DROP VIEW IF EXISTS raid_samples; DROP VIEW IF EXISTS raid_outcomes; "
                     "DROP VIEW IF EXISTS raid_members; DROP VIEW IF EXISTS raid_member_results; "
                     "DROP VIEW IF EXISTS capital_plans; DROP VIEW IF EXISTS capital_candidates; "
                     "DROP VIEW IF EXISTS capital_orders; DROP VIEW IF EXISTS capital_outcomes;")
    db.executescript(SCHEMA)
    if 'record' not in {row[1] for row in db.execute('PRAGMA table_info(events)')}:
        db.execute('ALTER TABLE events ADD COLUMN record TEXT')
    return db


def insert(db, row, source):
    if not isinstance(row, dict):
        raise ValueError('event must be a JSON object')
    if row.get('schema_version') != 1:
        raise ValueError('unsupported schema version')
    for key in ('session', 'event'):
        if not isinstance(row.get(key), str):
            raise ValueError(f'invalid {key}')
    if not isinstance(row.get('seq'), int) or not isinstance(row.get('data'), dict):
        raise ValueError('invalid seq/data')
    for key in ('cycle', 'house', 'player'):
        if row.get(key) is not None and not isinstance(row[key], int):
            raise ValueError(f'invalid {key}')
    existing = db.execute('SELECT record FROM events WHERE session=? AND seq=?',
                          (row['session'], row['seq'])).fetchone()
    if existing and existing[0] and json.loads(existing[0]) != row:
        raise ValueError('conflicting event for existing session/sequence')
    db.execute('INSERT OR IGNORE INTO events VALUES (?,?,?,?,?,?,?,?,?,?)', (
        row['session'], row['seq'], row.get('cycle'), row.get('house'), row.get('player'),
        row['event'], json.dumps(row['data'], ensure_ascii=False), row['schema_version'], str(source), json.dumps(row, ensure_ascii=False)))


def import_jsonl(db, path):
    count, bad, partial = 0, 0, 0
    with path.open(encoding='utf-8') as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.endswith('\n'):
                partial += 1  # Live writer/crash tail: retry on the next import.
                continue
            try:
                before = db.total_changes
                insert(db, json.loads(line), path)
                count += db.total_changes - before
            except (ValueError, TypeError, KeyError) as exc:
                bad += 1
                print(f'{path}:{line_number}: {exc}', file=sys.stderr)
            if line_number % 1000 == 0:
                db.commit()
    db.commit()
    return count, bad, partial


ZONE = re.compile(r'qBot\w+ \(([^)]+)\):\s+(?:CITY-ZONE|CITY-ECON): Building (Residential|Commercial|Industrial) Zone '
                  r'\(R:(\d+) C:(\d+) I:(\d+).*?valves=R([+-]\d+) C([+-]\d+) I([+-]\d+)')


def import_legacy(db, path):
    # Fingerprint the immutable preserved input; original lines are evidence,
    # not claimed as structured snapshots or accepted production orders.
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    session = 'legacy-' + digest.hexdigest()[:24]
    count = 0
    with path.open(encoding='utf-8', errors='replace') as stream:
        for number, line in enumerate(stream, 1):
            match = ZONE.search(line)
            if not match:
                continue
            house, choice, r, c, i, rd, cd, ind = match.groups()
            data = dict(zip(('res_count', 'com_count', 'ind_count', 'res_demand', 'com_demand', 'ind_demand'),
                            map(int, (r, c, i, rd, cd, ind))))
            data.update(house_name=house, choice=choice, source_line=number, observed_only=True)
            before = db.total_changes
            insert(db, dict(schema_version=1, session=session, seq=number, cycle=None,
                            house=None, player=None, event='legacy_zone_choice', data=data), path)
            count += db.total_changes - before
    db.commit()
    return count, 0, 0


def summary(db):
    print('Captured events:')
    for event, count in db.execute('SELECT event,count(*) FROM events GROUP BY event ORDER BY event'):
        print(f'  {event}: {count}')
    print('\nProduction orders by session / house / item / accepted:')
    for row in db.execute('SELECT session,house,item_name,accepted,count(*) FROM production GROUP BY 1,2,3,4'):
        print('  ' + ' | '.join(map(str, row)))
    print('\nLegacy residential selections despite stronger normalized C/I demand (observed selections only):')
    for row in db.execute("""SELECT json_extract(data,'$.house_name'), count(*),
        sum(CASE WHEN json_extract(data,'$.choice')='Residential' THEN 1 ELSE 0 END),
        sum(CASE WHEN json_extract(data,'$.choice')='Residential'
             AND json_extract(data,'$.res_demand')*3 <
                 max(json_extract(data,'$.com_demand'),json_extract(data,'$.ind_demand'))*4 THEN 1 ELSE 0 END)
        FROM events WHERE event='legacy_zone_choice' GROUP BY 1"""):
        print(f'  {row[0]}: {row[1]} choices, {row[2]} residential, {row[3]} with stronger jobs demand')


def audit(db):
    """Validate event references and cadence without conflating decisions with outcomes."""
    issues = []
    sessions = []
    for session, count, low, high in db.execute(
            "SELECT session,count(*),min(seq),max(seq) FROM events WHERE session NOT LIKE 'legacy-%' GROUP BY session"):
        if low != 1 or high != count:
            issues.append(f'{session}: non-contiguous sequence ({count} rows, {low}..{high})')
        starts = db.execute("SELECT count(*) FROM events WHERE session=? AND event='session_start'", (session,)).fetchone()[0]
        if starts != 1:
            issues.append(f'{session}: expected one session_start, got {starts}')
        sessions.append(dict(session=session, records=count, last_sequence=high))
    for reference, expected in [('state_id', 'state_snapshot'), ('zone_decision', 'zone_evaluation')]:
        rows = db.execute(f"""SELECT e.session,e.seq FROM events e LEFT JOIN events target
            ON target.session=e.session AND target.seq=json_extract(e.data,'$.{reference}')
            WHERE json_extract(e.data,'$.{reference}')>0 AND
            (target.seq IS NULL OR target.event!=? OR target.house!=e.house OR target.player!=e.player OR target.seq>=e.seq)""", (expected,)).fetchall()
        for session, seq in rows:
            issues.append(f'{session}:{seq}: invalid {reference} reference')
    for session, seq, raw in db.execute("SELECT session,seq,data FROM events WHERE event='zone_evaluation'"):
        d = json.loads(raw)
        selected = str(d['selected'])
        if selected == '4294967295':
            continue
        evaluation = d['evaluated']
        candidates = d['candidates']
        if evaluation.get(selected) != 'selected':
            issues.append(f'{session}:{seq}: selected zone does not match evaluation')
        chosen_rank = candidates[selected]['rank']
        for item, candidate in candidates.items():
            if 0 <= candidate['rank'] < chosen_rank and evaluation.get(item) not in ('unavailable', 'no_site'):
                issues.append(f'{session}:{seq}: skipped higher-ranked zone {item} without rejection')
    # Optional in older captures. Compare the same snapshot, never a stale
    # building census against a newer house total.
    for session, house, count, largest in db.execute("""SELECT session,house,count(*),
            max(abs(json_extract(data,'$.power_accounting.difference')))
            FROM events WHERE event='state_snapshot'
            AND json_extract(data,'$.power_accounting.difference') != 0 GROUP BY session,house"""):
        issues.append(f'{session}: house {house}: power accounting mismatch in {count} snapshots (max {largest})')
    return dict(sessions=sessions, issues=issues)


def behavior_report(db):
    result = []
    for session, house, player in db.execute("SELECT DISTINCT session,house,player FROM events WHERE event='state_snapshot'"):
        snapshots = db.execute("SELECT cycle,data FROM events WHERE session=? AND house=? AND player=? AND event='state_snapshot' ORDER BY seq",
                               (session, house, player)).fetchall()
        first_cycle, first = snapshots[0]
        last_cycle, last = snapshots[-1]
        first, last = json.loads(first), json.loads(last)
        meta = json.loads(db.execute("SELECT data FROM events WHERE session=? AND event='session_start'", (session,)).fetchone()[0])
        params = (session, house, player)
        orders = [dict(item=name, accepted=accepted, count=count) for name, accepted, count in db.execute(
            "SELECT item_name,accepted,count(*) FROM production WHERE session=? AND house=? AND player=? GROUP BY 1,2", params)]
        cancellations = dict(db.execute("SELECT json_extract(data,'$.item'),count(*) FROM events WHERE session=? AND house=? AND player=? AND event='placement_cancel' GROUP BY 1", params))
        choices = dict(db.execute("SELECT json_extract(data,'$.rule'),count(*) FROM events WHERE session=? AND house=? AND player=? AND event='construction_selection' GROUP BY 1", params))
        result.append(dict(session=session, house=house, player=player, name=last.get('house_name', str(house)),
            start_cycle=first_cycle, latest_cycle=last_cycle,
            latest_game_seconds=round(last_cycle*30/meta['cycles_per_30_seconds'], 1),
            initial_counts=first['actual'], latest_counts=last['actual'], latest_queued=last['queued'],
            latest_state=last['state'], latest_city_health=last.get('city_health',{}),
            heavy_busy=last['heavy_busy'], repair_busy=last['repair_busy'],
            production_orders=orders, placement_cancellations=cancellations, construction_rules=choices))
    return result


def match_report(db):
    """Whole-match evidence, including lifecycle caveats and ten-minute sample bins."""
    matches = []
    for (session,) in db.execute("SELECT session FROM events WHERE event='session_start' ORDER BY session"):
        rows = [dict(seq=seq, cycle=cycle or 0, house=house, player=player, event=event,
                     data=json.loads(data), envelope=json.loads(record) if record else {})
                for seq,cycle,house,player,event,data,record in db.execute(
                    'SELECT seq,cycle,house,player,event,data,record FROM events WHERE session=? ORDER BY seq', (session,))]
        meta = next(row['data'] for row in rows if row['event']=='session_start')
        cps = meta.get('cycles_per_30_seconds', 1875)/30
        last_cycle = max(row['cycle'] for row in rows)
        kinds = Counter(row['event'] for row in rows)
        item_names = {str(row['data']['item']): row['data']['item_name'] for row in rows
                      if row['event']=='production_order' and 'item_name' in row['data']}
        warnings = []
        if not kinds['session_end']: warnings.append('No clean session_end: capture may still be active or interrupted.')
        if kinds['capture_limit']: warnings.append('Capture reached its byte limit; later gameplay is absent.')
        if not kinds['game_summary']: warnings.append('No final roster/outcome; latest snapshots are not an official result.')
        if any(row['event']=='session_end' and row['cycle']<last_cycle for row in rows):
            warnings.append('Legacy session_end cycle is stale; duration uses the last gameplay event.')
        players = []
        for house,player in sorted({(row['house'],row['player']) for row in rows if row['event']=='state_snapshot'}):
            own = [row for row in rows if row['house']==house and row['player']==player]
            snapshots = [row for row in own if row['event']=='state_snapshot']
            samples = defaultdict(list)
            for row in snapshots: samples[int(row['cycle']/cps//600)].append(row['data'])
            phases=[]
            for phase, group in sorted(samples.items()):
                hf=sum(d['actual'].get('4',0) for d in group)
                phases.append(dict(start_minute=phase*10, samples=len(group),
                    median_credits=statistics.median(d['state']['credits'] for d in group),
                    median_military=statistics.median(d['state']['military'] for d in group),
                    busy_factory_fraction=round(sum(d.get('heavy_busy',0) for d in group)/hf,3) if hf else None,
                    low_cash_samples=sum(d['state']['credits']<1000 for d in group),
                    wealthy_no_factory_samples=sum(d['state']['credits']>20000 and not d['actual'].get('4',0) for d in group),
                    latest_actual=group[-1]['actual'],latest_city_health=group[-1].get('city_health',{})))
            orders=Counter(str(row['data']['item']) for row in own if row['event']=='production_order' and row['data'].get('accepted'))
            built=Counter(str(row['data']['item']) for row in own if row['event']=='object_built')
            lost=Counter(str(row['data']['item']) for row in own if row['event'] in ('unit_lost','structure_lost'))
            cancelled=Counter(str(row['data']['item']) for row in own if row['event']=='placement_cancel')
            items=[dict(item=int(item),name=item_names.get(item,item),orders=orders[item],built=built[item],lost=lost[item],cancelled=cancelled[item])
                   for item in sorted(set(orders)|set(built)|set(lost)|set(cancelled),key=int)]
            first_orders=[]; seen=set()
            for row in own:
                d=row['data']
                if row['event']=='production_order' and d.get('accepted') and d.get('builder_item')==2 and d['item'] not in seen:
                    seen.add(d['item']);first_orders.append(dict(minute=round(row['cycle']/cps/60,2),item=d.get('item_name',d['item']),builder=d['builder'],rule=d.get('rule')))
            zone_exceptions=[]
            for row in own:
                d=row['data']
                if row['event']!='zone_evaluation' or d['selected']!=20: continue
                state=d['state']
                if state['res_demand']*3 < max(state['com_demand'],state['ind_demand'])*4:
                    zone_exceptions.append(dict(seq=row['seq'],cycle=row['cycle'],evaluated=d['evaluated']))
            gates=Counter(row['data'].get('heavy_reason','unknown') for row in own if row['event']=='builder_status')
            attacks=[row for row in own if row['event']=='attack_launched']
            players.append(dict(house=house,player=player,initial=snapshots[0]['data'],latest=snapshots[-1]['data'],
                last_snapshot_cycle=snapshots[-1]['cycle'],phases=phases,items=items,first_construction_orders=first_orders,
                residential_over_stronger_jobs=zone_exceptions,factory_gates=dict(gates),
                attacks=dict(records=len(attacks),positive_new_force=sum(row['data'].get('force_value',0)>0 for row in attacks),
                    first_minute=round(attacks[0]['cycle']/cps/60,2) if attacks else None),
                attack_gates=dict(Counter(row['data'].get('reason') for row in own if row['event']=='attack_deferred')),
                # Army posture (Custom Hard/Brutal): the transitions, why they
                # happened, how many recall orders were issued, and whether the
                # outnumbered veto was lifted by the configured value share.
                # These make the recovery behaviour identifiable in a capture
                # without having to grep the raw events.
                posture=dict(
                    transitions=[dict(minute=round(row['cycle']/cps/60,2),
                                      to=row['data'].get('to'),reason=row['data'].get('reason'),
                                      emergency=bool(row['data'].get('emergency')))
                                 for row in own if row['event']=='army_posture'],
                    reasons=dict(Counter(row['data'].get('reason') for row in own
                                         if row['event']=='army_posture')),
                    recall_orders=sum(row['data'].get('issued',0) for row in own
                                      if row['event']=='army_recall'),
                    recall_passes=sum(1 for row in own if row['event']=='army_recall'),
                    recall_deferred=sum(1 for row in own if row['event']=='army_recall_deferred'),
                    legacy_hunters_adopted=sum(row['data'].get('adopted',0) for row in own
                                               if row['event']=='army_legacy_hunters_adopted'),
                    outnumbered_bypasses=sum(1 for row in own
                                             if row['event']=='attack_outnumbered_bypass'),
                    front_battery_sites=sum(1 for row in own
                                            if row['event']=='front_battery_site'
                                            and row['data'].get('valid')),
                    protected_rally_fallbacks=dict(Counter(
                        row['data'].get('fallback') for row in own
                        if row['event']=='protected_rally' and row['data'].get('fallback'))))))
        matches.append(dict(session=session,metadata=meta,last_cycle=last_cycle,minutes=round(last_cycle/cps/60,2),
            policies=sorted({row['envelope'].get('policy_version','unknown') for row in rows}),
            events=dict(kinds),warnings=warnings,players=players,
            final_summary=next((row['data'] for row in rows if row['event']=='game_summary'),None)))
    return matches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db', required=True, type=Path)
    sub = parser.add_subparsers(dest='command', required=True)
    imp = sub.add_parser('import')
    imp.add_argument('paths', nargs='+', type=Path)
    imp.add_argument('--legacy', action='store_true', help='Import observed zone selections from preserved text logs')
    sub.add_parser('summary')
    sub.add_parser('audit')
    sub.add_parser('report')
    sub.add_parser('match-report')
    query = sub.add_parser('query')
    query.add_argument('sql', help='Read-only SQL; events.data contains the full JSON payload')
    args = parser.parse_args()
    if args.command == 'import':
        totals = [0, 0, 0]
        with connect(args.db) as db:
            for path in args.paths:
                files = sorted(path.rglob('events.jsonl')) if path.is_dir() else [path]
                for file in files:
                    result = (import_legacy if args.legacy else import_jsonl)(db, file)
                    totals = [a + b for a, b in zip(totals, result)]
        print(f'Imported {totals[0]} new events; {totals[1]} invalid records; {totals[2]} incomplete tails deferred.')
        return bool(totals[1])
    with sqlite3.connect(args.db.resolve().as_uri() + '?mode=ro', uri=True) as db:
        if args.command == 'audit':
            result = audit(db)
            print(json.dumps(result, indent=2))
            return bool(result['issues'])
        elif args.command == 'match-report':
            print(json.dumps(match_report(db), indent=2))
        elif args.command == 'report':
            print(json.dumps(behavior_report(db), indent=2))
        elif args.command == 'summary':
            summary(db)
        else:
            cursor = db.execute(args.sql)
            print('\t'.join(column[0] for column in cursor.description))
            for row in cursor:
                print('\t'.join('' if x is None else str(x) for x in row))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
