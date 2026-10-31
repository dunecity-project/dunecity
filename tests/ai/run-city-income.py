#!/usr/bin/env python3
"""Measure what a controlled set of Dune City R/I/C zones actually pays.

Compiles the diagnostic main translation unit once (the same injection/link
pattern as run-campaign-balance.py: one TU with -fno-access-control linked
against the existing build objects) and runs the city-income probe with an
isolated profile and dummy SDL drivers. Harvester income is impossible: the
measured house keeps only the fixture and no unit/AI update runs.

Writes rows.csv / summary.json next to the run log. Reuses an already built
probe binary with --binary so several fixtures share one compile.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--binary', type=Path, help='Reuse an existing city-income probe binary')
parser.add_argument('--level', type=int, choices=range(1, 10), default=9)
parser.add_argument('--house', type=int, choices=(0,1,2), default=0, help='Harkonnen=0, Atreides=1, Ordos=2')
parser.add_argument('--mix', default='RICRIC', help='Zone order, e.g. RICRIC (2R/2I/2C) or RRRCCI (3R/1I/2C)')
parser.add_argument('--minutes', type=int, default=30)
parser.add_argument('--police', type=int, default=0, help='Police stations in the fixture (service cost)')
parser.add_argument('--tax', type=int, default=-1, help='City tax rate; -1 keeps the engine default')
parser.add_argument('--cash', type=int, default=10000, help='Fixture cash injection (excluded from income)')
parser.add_argument('--seed', type=int, default=486409243)
parser.add_argument('--label', default=None)
parser.add_argument('--wall-timeout', type=int, default=1800)
parser.add_argument('--game-options', type=Path, help='INI with a [Game Options] section applied last (e.g. Slow City Year = false)')
args = parser.parse_args()
if not 1 <= args.minutes <= 120 or not set(args.mix) <= set('RIC') or not args.mix:
    parser.error('Use 1-120 minutes and a zone mix of R/I/C letters.')
label = args.label or f'{args.mix}-{args.minutes}min-seed{args.seed}-police{args.police}'
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

if args.binary:
    binary = args.binary.resolve()
else:
    subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
    target = 'bin/dunecity.app/Contents/MacOS/dunecity'
    commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
    main = (root / 'src/main.cpp').read_text()
    needle = 'int menuResult = MainMenu().showMenu();'
    if main.count(needle) != 1:
        raise RuntimeError('Main-menu injection point changed.')
    main = main.replace(needle, 'int menuResult = runCampaignBalanceProbe();')
    main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
    position = main.index('int main(')
    main = main[:position] + '#include "' + str(root / 'tests/ai/campaign-balance-probe.inc') + '"\n' + main[position:]
    source, obj = out / 'city-income-main.cpp', out / 'city-income-main.o'
    source.write_text(main)
    compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
    for option in ('-include', '-MT', '-MF'):
        if option in compile_command:
            position = compile_command.index(option)
            del compile_command[position:position + 2]
    for option in ('-MD', '-MMD'):
        if option in compile_command:
            compile_command.remove(option)
    compile_command[compile_command.index('-o') + 1] = str(obj)
    compile_command[compile_command.index('-c') + 1] = str(source)
    compile_command.append('-fno-access-control')
    app = out / 'city-income-probe.app/Contents'
    (app / 'MacOS').mkdir(parents=True, exist_ok=True)
    if not (app / 'Resources').exists():
        (app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
    binary = app / 'MacOS/city-income-probe'
    link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
    link = link[link.index('&&') + 1:]
    link = link[:link.index('&&')]
    link[link.index('-o') + 1] = str(binary)
    link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
    with (out / 'build.log').open('w') as log:
        subprocess.run(compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

env = dict({k:v for k,v in os.environ.items() if not k.startswith(('BALANCE_', 'CITY_INCOME_'))}, DUNECITY_USERDIR=str(out / 'profile'), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
           BALANCE_MOD='dunecity', BALANCE_LEVEL=str(args.level), BALANCE_PARTNER='easy', BALANCE_ENEMY='easy',
           BALANCE_ENEMY_AI='quantbot', BALANCE_SEED=str(args.seed), BALANCE_MINUTES='1',
           BALANCE_ATTACK_PERCENT='0', BALANCE_HOUSE=str(args.house), BALANCE_HARVESTER_LIMIT='-1',
           BALANCE_CITY_INCOME_PROBE='1', CITY_INCOME_MIX=args.mix, CITY_INCOME_MINUTES=str(args.minutes),
           CITY_INCOME_POLICE=str(args.police), CITY_INCOME_TAX=str(args.tax), CITY_INCOME_CASH=str(args.cash),
           CITY_INCOME_LABEL=label)
if args.game_options: env['BALANCE_GAME_OPTIONS'] = str(args.game_options.resolve())
runlog = out / f'run-{label}.log'
with runlog.open('w') as log:
    subprocess.run([str(binary), '--window', '--showlog'], cwd=out, env=env,
                   stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.wall_timeout)

text = runlog.read_text()


def parse(line):
    return {key: value for key, value in re.findall(r'(\w+)=(\[[^\]]*\]|[^\s]+)', line.split(': ', 1)[1])}


rows = [parse(line) for line in text.splitlines() if 'CITY_INCOME_ROW:' in line]
setups = [parse(line) for line in text.splitlines() if 'CITY_INCOME_SETUP:' in line]
summaries = [parse(line) for line in text.splitlines() if 'CITY_INCOME_SUMMARY:' in line]
if len(summaries) != 1 or len(setups) != 1 or not rows:
    raise RuntimeError('Probe produced no complete measurement.')
summary, setup = summaries[0], setups[0]

with (out / f'rows-{label}.csv').open('w', newline='') as handle:
    writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)

gross = float(summary['gross'])
police_paid = float(summary['police_paid'])
power_paid = float(summary['power_paid'])
net_credits = float(summary['net_credits'])
cycles = float(summary['cycles'])
minute_rows = [row for row in rows if row['kind'] == 'minute']
boundary = max(0, args.minutes - 10)
final10 = [row for row in rows if boundary <= float(row['minute']) <= args.minutes and row['kind'] != 'end']
warmup = [row for row in rows if float(row['minute']) <= boundary and row['kind'] != 'end']


def window(selection):
    if len(selection) < 2:
        return None
    first, last = selection[0], selection[-1]
    span = (float(last['cycle']) - float(first['cycle'])) / 3750.0
    return {
        'minutes': span,
        'gross': float(last['gross_receipts']) - float(first['gross_receipts']),
        'gross_per_minute': (float(last['gross_receipts']) - float(first['gross_receipts'])) / span,
        'police_per_minute': (float(last['police_paid']) - float(first['police_paid'])) / span,
        'power_per_minute': (float(last['power_paid']) - float(first['power_paid'])) / span,
        'net_credits': float(last['credits']) - float(first['credits']),
        'net_per_minute': (float(last['credits']) - float(first['credits'])) / span,
        'from_cycle': first['cycle'], 'to_cycle': last['cycle'],
    }


if not summary['reconciled'] == '1' or abs(gross - police_paid - power_paid - net_credits) > 1.0:
    raise RuntimeError('Income accounting does not reconcile')

report = {
    'label': label, 'setup': setup, 'summary': summary,
    'settings': {**vars(args), 'label': label},
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'working_tree_modified': bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()),
    'cadence': {'cycles_per_city_year': int(setup['cycles_per_city_year']),
                'cycles_per_city_day': int(setup['cycles_per_city_day']),
                'budget_ticks_per_year': int(setup['budget_ticks_per_year']),
                'elapsed_cycles': cycles, 'simulated_minutes': cycles / 3750.0},
    'totals': {'gross': gross, 'police_paid': police_paid, 'power_paid': power_paid,
               'net_credits': net_credits, 'gross_per_minute': gross / (cycles / 3750.0),
               'net_per_minute': net_credits / (cycles / 3750.0),
               'reconciles': abs(gross - police_paid - power_paid - net_credits) <= 1.0,
               'fixture_cash_excluded': int(summary['fixture_cash'])},
    'windows': {'warmup': window(warmup), 'final10': window(final10)},
    'final_row': rows[-1], 'rows_csv': str(out / f'rows-{label}.csv'), 'run_log': str(runlog),
}
(out / f'summary-{label}.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
print(json.dumps({k: report[k] for k in ('label', 'cadence', 'totals', 'windows')}, indent=2, default=str))
print('Final row:', rows[-1].get('lots'), 'tax_base_eighths=' + rows[-1]['tax_base_eighths'],
      'avg_land_value=' + rows[-1]['avg_land_value'])
