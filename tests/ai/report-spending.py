#!/usr/bin/env python3
"""Summarise shared spending decisions from native or exported browser events.jsonl.

Reports complete candidate comparisons, linked orders, rejected purchases and
budget violations. Never treats a truncated capture as a complete match.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('events', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--check', action='store_true', help='Fail on broken links, overspending or city orders in vanilla')
args = parser.parse_args()
plans = {}
houses = defaultdict(lambda: {'plans': 0, 'choices': Counter(), 'orders': Counter(),
                              'spending': Counter(), 'blocked': Counter(), 'last_forecast': {}})
violations, capture_limits = [], []
session_ended = False
for line_number, line in enumerate(args.events.open(), 1):
    try:
        row = json.loads(line)
    except json.JSONDecodeError:
        violations.append(f'Invalid/incomplete JSON at line {line_number}')
        continue
    event, data = row['event'], row['data']
    if event == 'session_end':
        session_ended = True
    if event == 'capture_limit':
        capture_limits.append({'cycle': row['cycle'], **data})
    if event == 'capital_plan':
        plans[row['seq']] = {'house': row['house'], 'data': data, 'spent': 0, 'emergency': False}
        house = houses[row['house']]
        house['plans'] += 1
        choice = data['candidates'].get(str(data['selected']), {})
        house['choices'][choice.get('kind', 'none')] += 1
        house['last_forecast'] = {k: data[k] for k in ('mode', 'spendable', 'forecast_net_income',
            'forecast_funding', 'military_value', 'military_target', 'workers', 'worker_target', 'spice_share')}
    elif event == 'production_order' and data.get('capital_plan'):
        plan = plans.get(data['capital_plan'])
        if not plan or plan['house'] != row['house']:
            violations.append(f'Order {row["seq"]} lacks a matching house/plan')
            continue
        if not data['accepted']:
            continue
        price = data['quoted_price']
        house = houses[row['house']]
        house['orders'][data['item_name']] += 1
        house['spending'][data['item_name']] += price
        plan['spent'] += price
        state = data['state']
        # Genuine blackout recovery may queue a generator before its full cost
        # is available; normal purchases never use forecast income as cash.
        if state['power_produced'] < state['power_required'] and data['rule'] in ('power', 'campaign_required_power', 'campaign_planned_power'):
            plan['emergency'] = True
        if plan['data']['mode'] == 'vanilla' and data['item'] in (20, 21, 22):
            violations.append(f'Vanilla order {row["seq"]} built an R/C/I zone')
    elif event in ('capital_upgrade', 'capital_road_batch') and data.get('capital_plan'):
        plan = plans.get(data['capital_plan'])
        if not plan or plan['house'] != row['house']:
            violations.append(f'Auxiliary order {row["seq"]} lacks a matching house/plan')
        elif event == 'capital_road_batch' or data['accepted']:
            plan['spent'] += data['cost'] if event == 'capital_road_batch' else data['price']
    elif event == 'research_started' and data.get('capital_plan'):
        plan = plans.get(data['capital_plan'])
        if not plan or plan['house'] != row['house']:
            violations.append(f'Research {row["seq"]} lacks a matching house/plan')
        else:
            plan['spent'] += data['cost']
            houses[row['house']]['orders']['Tech research'] += 1
            houses[row['house']]['spending']['Tech research'] += data['cost']
    elif event == 'capital_outcome' and data.get('plan'):
        plan = plans.get(data['plan'])
        if plan and data['ordered_cost'] != plan['spent']:
            violations.append(f'Plan {data["plan"]} cost {data["ordered_cost"]} differs from logged orders {plan["spent"]}')
    elif event == 'capital_order_blocked':
        houses[row['house']]['blocked'][data['reason']] += 1
for seq, plan in plans.items():
    if plan['spent'] > plan['data']['spendable'] and not plan['emergency']:
        violations.append(f'Plan {seq} ordered {plan["spent"]} with {plan["data"]["spendable"]} spendable')
result = {'source': str(args.events.resolve()), 'capture_complete': session_ended and not capture_limits,
          'capture_limits': capture_limits,
          'houses': dict(houses), 'violations': violations}
output = json.dumps(result, indent=2) + '\n'
if args.output:
    args.output.write_text(output)
else:
    print(output, end='')
if args.check and capture_limits:
    raise SystemExit('Capture reached its limit; comparison is incomplete')
if args.check and violations:
    raise SystemExit(f'{len(violations)} spending audit violation(s); see report')
