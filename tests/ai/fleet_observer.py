"""Pure fleet-observer CSV contracts and log decoding, shared by AI harnesses."""
import json
from collections import defaultdict

FLEET_VALUES = (
    'refined_spice', 'refined_spice_raw', 'carryalls', 'repair_yards', 'harvesters_built', 'harvesters_lost',
    'carryalls_built', 'refineries_built', 'repair_yards_built', 'fleet_capital_current_value',
    'fleet_capital_built_value', 'fleet_capital_value_cycles', 'fleet_steps', 'fleet_observed_cycles',
    'fleet_gap_count', 'fleet_gap_cycles', 'fleet_max_step_cycles', 'fleet_harvester_cycles',
    'fleet_bay_available_cycles', 'fleet_bay_occupied_cycles', 'fleet_bay_unloading_cycles',
    'refinery_bays', 'occupied_refinery_bays', 'unloading_refinery_bays',
    'harvester_unload_starts', 'harvester_unload_censored', 'harvester_trip_count',
    'harvester_trip_cycles_sum', 'harvester_trip_cycles_min', 'harvester_trip_cycles_max',
    'harvester_trip_left_censored', 'harvester_trip_left_censored_cycles', 'harvester_trip_censored',
    'harvester_trip_censored_cycles', 'harvester_trip_open', 'harvester_trip_open_cycles',
    'harvester_trip_unstarted', 'harvester_trip_unstarted_cycles', 'harvester_trip_unstarted_censored',
    'harvester_trip_unstarted_censored_cycles', 'fleet_geometry_samples', 'fleet_geometry_cycle',
    'fleet_refinery_metrics')
REFINERY_GEOMETRY_VALUES = (
    'geometry_cycle', 'nearest_spice_distance_steps', 'nearest_spice_field_id', 'nearest_spice_field_tiles',
    'nearest_spice_field_amount', 'spice_component_id', 'spice_component_tiles', 'spice_component_amount',
    'accessible_spice_tiles', 'accessible_spice_amount')
REFINERY_VALUES = ('minute', 'cycle', 'house', 'refinery', 'x', 'y') + REFINERY_GEOMETRY_VALUES + (
    'refinery_bays', 'occupied_refinery_bays', 'unloading_refinery_bays', 'fleet_bay_available_cycles',
    'fleet_bay_occupied_cycles', 'fleet_bay_unloading_cycles')


def fleet_observations(lines):
    """Deduplicate pure observer rows, retaining blank and censoring semantics."""
    def fields(line, marker):
        return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)

    houses, refineries = {}, {}
    for line in lines:
        if 'DUNECITY_FLEET:' in line:
            row = fields(line, 'DUNECITY_FLEET:')
            row.update(json.loads(row.pop('fleet')))
            for key in ('harvester_trip_cycles_min', 'harvester_trip_cycles_max', 'fleet_geometry_cycle'):
                if row.get(key) in (-1, '-1'):
                    row[key] = ''
            houses[(int(row['house']), int(row['cycle']))] = row
        elif 'DUNECITY_REFINERY:' in line:
            row = fields(line, 'DUNECITY_REFINERY:')
            if int(row['geometry_cycle']) < 0:
                row.update(dict.fromkeys(REFINERY_GEOMETRY_VALUES, ''))
            else:
                if int(row['nearest_spice_distance_steps']) < 0:
                    row.update(dict.fromkeys(('nearest_spice_distance_steps', 'nearest_spice_field_id',
                                              'nearest_spice_field_tiles', 'nearest_spice_field_amount'), ''))
                if int(row['spice_component_id']) < 0:
                    row.update(dict.fromkeys(('spice_component_id', 'spice_component_tiles',
                                              'spice_component_amount'), ''))
            refineries[(int(row['house']), int(row['refinery']), int(row['cycle']))] = row
    by_sample = defaultdict(list)
    refinery_rows = [row for _, row in sorted(refineries.items())]
    for row in refinery_rows:
        by_sample[(int(row['house']), int(row['cycle']))].append(
            {key: value for key, value in row.items() if key not in ('house', 'cycle', 'minute')})
    for key, row in houses.items():
        row['fleet_refinery_metrics'] = json.dumps(by_sample[key], sort_keys=True, separators=(',', ':'))
    return houses, refinery_rows
