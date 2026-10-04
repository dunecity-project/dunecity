#!/usr/bin/env python3
"""Run real campaign AI with a fixed seed, isolated profile and no human commands.

Uses the existing macOS Ninja build. Access-control relaxation is restricted to
this diagnostic executable; no test hooks are compiled into the shipped game.
--attack-percent sets the legacy Easy fraction (zero disables dispatch); campaign
Easy/Medium use the half-army policy plus per-house wave caps. Results
and structured AI decision logs remain in --output-dir. This is a simulation
comparison, not a substitute for the browser playtest or human playtesting.
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
house_names = ('harkonnen','atreides','ordos','fremen','sardaukar','mercenary','neutral','rebels','custom','wildspade','kleshmersh','tharpique')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--repeatable', action='store_true', help='Keep each CTest run in a fresh subdirectory')
parser.add_argument('--custom-map', type=Path, help='Run all occupied slots of a custom map instead of the campaign')
parser.add_argument('--free-for-all', action='store_true', help='Give each custom-map house its own team')
parser.add_argument('--capture-mib', type=int, default=1024, help='Diagnostic capture allowance; shipped default is unchanged')
parser.add_argument('--wall-timeout', type=int, default=1800, help='Maximum wall seconds for the simulation')
parser.add_argument('--level', type=int, choices=range(1,10), default=4)
parser.add_argument('--mod', choices=('vanilla','dunecity','Dune2R','Tornie'), default='vanilla')
parser.add_argument('--house', choices=tuple(h for h in house_names if h!='neutral'), default='harkonnen')
parser.add_argument('--roster', help='Explicit custom-map house:team slots in lobby order, comma-separated')
parser.add_argument('--harvester-limit', type=int, choices=range(-1,101), default=-1)
parser.add_argument('--rocket-turrets-need-power', action=argparse.BooleanOptionalAction, default=None)
parser.add_argument('--concrete-required', action=argparse.BooleanOptionalAction, default=None)
parser.add_argument('--require-win', action='store_true', help='Fail a full-match run on defeat or time limit')
parser.add_argument('--partner-difficulty', choices=('easy','medium','hard','brutal'), default='easy')
parser.add_argument('--enemy-ai', choices=('quantbot','ai-player'), default='quantbot', help='Enemy controller family; AI Player has Easy/Medium/Hard')
parser.add_argument('--enemy-difficulty', choices=('easy','medium','hard','brutal'), default='easy')
parser.add_argument('--shared-spending-probe', action='store_true')
parser.add_argument('--controls-probe', action='store_true')
parser.add_argument('--sourceforge-probe', action='store_true')
parser.add_argument('--harvester-safety-probe', action='store_true')
parser.add_argument('--city-placement-probe', action='store_true')
parser.add_argument('--opening-economy-probe', action='store_true')
parser.add_argument('--starport-probe', action='store_true', help='Exercise reserved cash with above-normal Starport prices')
parser.add_argument('--helper-economy-probe', action='store_true', help='Verify advanced campaign helper worker investment and paid imports')
parser.add_argument('--city-campaign-probe', action='store_true', help='Verify campaign city limits, permissions, depletion and save/load')
parser.add_argument('--spice-income-probe', action='store_true',
                    help='Verify the per-house spice delivery income factor against real refinery unloading')
parser.add_argument('--credit-storage-probe', action='store_true',
                    help='Verify the shared spice/tax storage limit, exempt starting cash and refunds')
parser.add_argument('--stats-probe', action='store_true', help='Verify campaign results with a shared human/AI house')
parser.add_argument('--nuclear-probe', action='store_true')
parser.add_argument('--reactor-safety-probe', action='store_true')
parser.add_argument('--degradation-probe', action='store_true',
                    help='Verify Dynasty-aligned power damage, foundation decay and their save state')
parser.add_argument('--radar-probe', action='store_true')
parser.add_argument('--army-probe', action='store_true')
parser.add_argument('--path-workers', type=int, default=0,
                    help='Pin the pathfinding worker count (0 leaves the engine default). '
                         'Used to show that a probe reaches the same result under one and '
                         'four workers.')
parser.add_argument('--recovery-probe', action='store_true',
                    help='Verify Custom Hard/Brutal army recovery, wave cohesion, the outnumbered '
                         'dispatch gate, enemy-facing batteries and the special-unit allocator')
parser.add_argument('--battery-clearance-probe', action='store_true',
                    help='Verify crowded-city rocket batteries replace eligible R/I/C lots through real production')
parser.add_argument('--air-defense-probe', action='store_true')
parser.add_argument('--police-placement-probe', action='store_true')
parser.add_argument('--police-budget-probe', action='store_true')
parser.add_argument('--police-reinforcement-probe', action='store_true',
                    help='Verify police reinforcements follow the selected unit limit, not a police-local ceiling')
parser.add_argument('--city-growth-probe', action='store_true',
                    help='Verify continuous city growth while proactive defence/supplier goals are unmet')
parser.add_argument('--mcv-deployment-probe', action='store_true',
                    help='Verify MCVs deploy on the rock the base already holds instead of driving around it')
parser.add_argument('--custom-attack-probe', action='store_true')
parser.add_argument('--whole-army-probe', action='store_true',
                    help='Verify the Custom Hard/Brutal whole-army dispatch and asset emergency defence')
parser.add_argument('--factory-recovery-probe', action='store_true')
parser.add_argument('--pressure-probe', action='store_true', help='Verify campaign wave readiness, survivor independence and save state')
parser.add_argument('--defence-probe', action='store_true', help='Verify retaliation and base/harvester reinforcements')
parser.add_argument('--repair-probe', action='store_true', help='Verify experienced campaign bots replace missing repair yards')
parser.add_argument('--pacing-probe', action='store_true', help='Verify enemy worker caps and small-wave readiness')
parser.add_argument('--seed', type=int, default=486409243)
parser.add_argument('--minutes', type=int, default=20)
parser.add_argument('--attack-percent', type=int, choices=range(101), default=25)
args = parser.parse_args()
if not 1 <= args.minutes <= 120 or not 0 <= args.seed <= 0xffffffff:
    parser.error('Use 1–120 minutes and a 32-bit unsigned seed.')
if not 256 <= args.capture_mib <= 4096 or args.wall_timeout < 1:
    parser.error('Use 256–4096 MiB of capture space and a positive wall timeout.')
if args.free_for_all and not args.custom_map:
    parser.error('--free-for-all requires --custom-map.')
if args.enemy_ai == 'ai-player' and args.enemy_difficulty == 'brutal':
    parser.error('AI Player has no Brutal controller.')
if not args.custom_map and (args.roster or args.house not in house_names[:3]):
    parser.error('Explicit rosters and additional houses require --custom-map.')
roster = []
if args.custom_map:
    scenario = configparser.ConfigParser(strict=False, interpolation=None)
    with args.custom_map.open() as source_file:
        scenario.read_file(source_file)
    sections = {s.lower(): s for s in scenario.sections()}
    names = house_names
    assigned = set()
    for name in names:
        if name in sections:
            brain = scenario[sections[name]].get('brain', 'Team2').lower()
            team = int(brain[4:]) if brain.startswith('team') else (1 if name == args.house else 2)
            roster.append((names.index(name), team))
            assigned.add(name)
    # Generic slots use the chosen player house first, then distinct opponents.
    # The engine still chooses their spawn slots using the supplied match seed.
    candidates = [args.house] + [n for n in ('harkonnen', 'atreides', 'ordos', 'sardaukar', 'fremen', 'mercenary') if n != args.house]
    for slot in range(1, 7):
        section = sections.get(f'player{slot}')
        if not section:
            continue
        name = next((n for n in candidates if n not in assigned), None)
        if name is None:
            parser.error('Map has more occupied slots than supported houses.')
        brain = scenario[section].get('brain', f'Team{slot}').lower()
        team = int(brain[4:]) if brain.startswith('team') else slot
        roster.append((names.index(name), team))
        assigned.add(name)
    if args.house not in assigned:
        parser.error('Chosen player house has no slot on this map.')
    if args.roster:
        try:
            requested = [(names.index(name), int(team)) for name,team in
                         (slot.split(':') for slot in args.roster.lower().split(','))]
        except ValueError:
            parser.error('Roster must contain known house:team entries.')
        if (len(requested)!=len(roster) or len({h for h,_ in requested})!=len(requested)
                or any(t<1 for h,t in requested)
                or names.index(args.house) not in {h for h,_ in requested}):
            parser.error('Roster must match map slot count, use distinct playable houses and include --house.')
        roster=requested
    if args.free_for_all:
        roster = [(house, i + 1) for i, (house, _) in enumerate(roster)]
build, out = args.build_dir.resolve(), args.output_dir.resolve()
source_commit = subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
source_modified = bool(subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).strip())
if args.repeatable:
    out.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=out))
else:
    out.mkdir(parents=True, exist_ok=False)
subprocess.run(['python3',str(root/'scripts/check-build-deps.py'),str(build)],check=True,cwd=root)
target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True).splitlines()
main = (root/'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle,'int menuResult = runCampaignBalanceProbe();')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))','if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "'+str(root/'tests/ai/campaign-balance-probe.inc')+'"\n' + main[position:]
source, obj = out/'balance-main.cpp', out/'balance-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include','-MT','-MF'):
    if option in compile_command:
        position = compile_command.index(option)
        del compile_command[position:position+2]
for option in ('-MD','-MMD'):
    if option in compile_command: compile_command.remove(option)
compile_command[compile_command.index('-o')+1] = str(obj)
compile_command[compile_command.index('-c')+1] = str(source)
compile_command.append('-fno-access-control')
app = out/'balance-probe.app/Contents'
(app/'MacOS').mkdir(parents=True)
(app/'Resources').symlink_to(build/'bin/dunecity.app/Contents/Resources')
binary = app/'MacOS/balance-probe'
link = shlex.split(next(line for line in commands if ' -o '+target+' ' in line))
link = link[link.index('&&')+1:]
link = link[:link.index('&&')]
link[link.index('-o')+1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (out/'build.log').open('w') as log:
    subprocess.run(compile_command,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
    subprocess.run(link,cwd=build,stdout=log,stderr=subprocess.STDOUT,check=True)
env = dict(os.environ,DUNECITY_USERDIR=str(out/'profile'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
           BALANCE_MOD=args.mod,BALANCE_LEVEL=str(args.level),BALANCE_PARTNER=args.partner_difficulty,BALANCE_SEED=str(args.seed),BALANCE_MINUTES=str(args.minutes),
           BALANCE_ATTACK_PERCENT=str(args.attack_percent),BALANCE_ENEMY=args.enemy_difficulty,BALANCE_ENEMY_AI=args.enemy_ai,
           BALANCE_HOUSE=str(house_names.index(args.house)),BALANCE_HARVESTER_LIMIT=str(args.harvester_limit))
env['BALANCE_CAPTURE_MIB'] = str(args.capture_mib)
if args.rocket_turrets_need_power is not None:
    env['BALANCE_ROCKET_TURRETS_NEED_POWER'] = str(int(args.rocket_turrets_need_power))
if args.concrete_required is not None:
    env['BALANCE_CONCRETE_REQUIRED'] = str(int(args.concrete_required))
if args.custom_map:
    env['BALANCE_CUSTOM_MAP'] = str(args.custom_map.resolve())
    env['BALANCE_ROSTER'] = ','.join(f'{house}:{team}' for house, team in roster)
(out/'setup.json').write_text(json.dumps({**vars(args), 'resolved_roster': roster}, default=str, indent=2)+'\n')
if args.shared_spending_probe: env['BALANCE_SHARED_SPENDING_PROBE'] = '1'
if args.harvester_safety_probe: env['BALANCE_HARVESTER_SAFETY_PROBE'] = '1'
if args.sourceforge_probe: env['BALANCE_SOURCEFORGE_PROBE'] = '1'
if args.controls_probe or args.sourceforge_probe:
    if args.controls_probe: env['BALANCE_CONTROLS_PROBE'] = '1'
    profile = out/'profile'
    profile.mkdir(exist_ok=True)
    (profile/'Dune City.ini').write_text('[Video]\nPhysical Width = 640\nPhysical Height = 480\nWidth = 640\nHeight = 480\nInterface Height = 480\nFullscreen = false\n[General]\nPlay Intro = false\n')
if args.city_placement_probe: env['BALANCE_CITY_PLACEMENT_PROBE'] = '1'
if args.opening_economy_probe: env['BALANCE_OPENING_ECONOMY_PROBE'] = '1'
if args.nuclear_probe or args.reactor_safety_probe: env['BALANCE_NUCLEAR_PROBE'] = '1'
if args.reactor_safety_probe: env['BALANCE_REACTOR_SAFETY_PROBE'] = '1'
if args.degradation_probe: env['BALANCE_DEGRADATION_PROBE'] = '1'
if args.radar_probe: env['BALANCE_RADAR_PROBE'] = '1'
if args.army_probe: env['BALANCE_ARMY_PROBE'] = '1'
if args.recovery_probe: env['BALANCE_RECOVERY_PROBE'] = '1'
if args.battery_clearance_probe:
    env['BALANCE_RECOVERY_PROBE'] = '1'
    env['BALANCE_BATTERY_CLEARANCE_ONLY'] = '1'
if args.path_workers > 0: env['DUNECITY_PATH_WORKERS'] = str(args.path_workers)
if args.air_defense_probe: env['BALANCE_AIR_DEFENSE_PROBE'] = '1'
if args.police_placement_probe: env['BALANCE_POLICE_PLACEMENT_PROBE'] = '1'
if args.police_budget_probe: env['BALANCE_POLICE_BUDGET_PROBE'] = '1'
if args.police_reinforcement_probe: env['BALANCE_POLICE_REINFORCEMENT_PROBE'] = '1'
if args.city_growth_probe: env['BALANCE_CITY_GROWTH_PROBE'] = '1'
if args.mcv_deployment_probe: env['BALANCE_MCV_DEPLOYMENT_PROBE'] = '1'
if args.custom_attack_probe: env['BALANCE_CUSTOM_ATTACK_PROBE'] = '1'
if args.whole_army_probe: env['BALANCE_WHOLE_ARMY_PROBE'] = '1'
if args.factory_recovery_probe: env['BALANCE_FACTORY_RECOVERY_PROBE'] = '1'
if args.starport_probe: env['BALANCE_STARPORT_PROBE'] = '1'
if args.helper_economy_probe: env['BALANCE_HELPER_ECONOMY_PROBE'] = '1'
if args.city_campaign_probe: env['BALANCE_CITY_CAMPAIGN_PROBE'] = '1'
if args.spice_income_probe: env['BALANCE_SPICE_INCOME_PROBE'] = '1'
if args.credit_storage_probe: env['BALANCE_CREDIT_STORAGE_PROBE'] = '1'
if args.stats_probe: env['BALANCE_STATS_PROBE'] = '1'
if args.pressure_probe: env['BALANCE_PRESSURE_PROBE'] = '1'
if args.defence_probe: env['BALANCE_DEFENCE_PROBE'] = '1'
if args.pacing_probe: env['BALANCE_PACING_PROBE'] = '1'
if args.repair_probe: env['BALANCE_REPAIR_PROBE'] = '1'
with (out/'run.log').open('w') as log:
    subprocess.run([str(binary),'--window','--showlog'],cwd=out,env=env,
                   stdout=log,stderr=subprocess.STDOUT,check=True,timeout=args.wall_timeout)
results = [line for line in (out/'run.log').read_text().splitlines() if 'CAMPAIGN_BALANCE_RESULT:' in line]
if len(results) != 1: raise RuntimeError('Missing campaign result.')
events = next((out/'profile').rglob('events.jsonl'))
metadata, attacks, final = None, [], []
for line in events.open():
    row = json.loads(line)
    if row['event'] == 'session_start': metadata = row['data']
    if row['event'] == 'ground_hunt': attacks.append(row)
    if row['event'] == 'game_summary': final.append(row)
    # The pressure fixture switches difficulty in-engine and checks each tier
    # itself; its Hard/Brutal waves must not inherit the CLI's default Easy cap.
    if not args.pressure_probe and row['event']=='ground_hunt' and row['data'].get('campaign_limited') and args.enemy_difficulty in ('easy','medium','hard'):
        d=row['data']
        # Caps apply to this dispatch. Surviving units from older waves are
        # deliberately allowed alongside it after the repeat timer expires.
        if (d['members']>d['alliance_unit_cap']
                or d['value']>d['alliance_value_cap']
                or d['alliance_units']>d['alliance_unit_cap']
                or d['alliance_value']>d['alliance_value_cap']
                or d['alliance_value']>d['attack_budget']):
            raise RuntimeError('Automatic campaign force exceeded its wave budget')
summary = {'result':results[0],'sourceCommit':source_commit,
           'workingTreeModified':source_modified,
           'metadata':metadata,'attacks':attacks,'final':final,'resolved_roster':roster,'enemy_controller':args.enemy_ai,'harvester_limit':args.harvester_limit}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(results[0])
print('Telemetry:',events)

if args.require_win and 'outcome=won' not in results[0]:
    raise RuntimeError('Full-match acceptance requires victory: ' + results[0])
