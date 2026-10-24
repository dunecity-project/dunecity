#!/usr/bin/env python3
"""Screenshot and lint every skin x resolution x screen against the real menus and a real game.

Links a diagnostic copy of the game whose main menu is replaced by tests/menu/ui-gallery-probe.inc
(production objects are unchanged) and runs one isolated-profile process per skin and display
case. Each process renders the menus, a played DuneCity skirmish (sidebar, inspect panels incl.
Natives' worm and crime trooper, chat, ticker, F12, pause, city budget, house statistics) and a
spectated game's end plate in every house colour slot plus its statistics, then applies the next
skin from the real Options menu and back (the in-game sidebar must come back pixel-identical).
Also checks that a new profile starts in Classic Desert and that a 1.0.850 profile (no UI Skin
key) gets it too.

Writes PNGs under OUTPUT/<skin>/<case>/, a contact sheet per skin and case (contact/), and
summary.txt / issues.tsv. Exit status 1 when a process fails (broken behaviour); layout findings
are listed, with those that only one desert skin has (not Modern) marked as skin regressions.
"""
import argparse
import concurrent.futures
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
SKINS = {0: 'modern', 1: 'lapis', 2: 'classic'}
# name: physical size, Interface Height preset, the logical size the game must then use
CASES = {
    '640x480':        ((640, 480), 480, (640, 480)),
    '854x480':        ((854, 480), 480, (854, 480)),
    '1024x768':       ((1024, 768), 768, (1024, 768)),
    '1280x720':       ((1280, 720), 0, (1280, 720)),
    '1440x900-hd':    ((1440, 900), 600, (960, 600)),      # Stefan's MBA: Medium, ScaleHD
    '1920x1080-native': ((1920, 1080), -1, (1920, 1080)),
    '1920x1080-scaled': ((1920, 1080), 768, (1364, 768)),
}

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--build-dir', type=Path, default=root / 'build')
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--skins', type=int, nargs='+', default=[0, 1, 2], choices=[0, 1, 2])
parser.add_argument('--cases', nargs='+', default=list(CASES), choices=list(CASES))
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--timeout', type=int, default=900)
parser.add_argument('--keep', action='store_true', help='Keep profiles and the probe executable')
args = parser.parse_args()
build, out = args.build_dir.resolve(), args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)

target = 'bin/dunecity.app/Contents/MacOS/dunecity'
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', target], text=True).splitlines()
main = (root / 'src/main.cpp').read_text()
needle = 'int menuResult = MainMenu().showMenu();'
if main.count(needle) != 1:
    raise RuntimeError('Main-menu injection point changed.')
main = main.replace(needle, 'int menuResult = runUIGalleryProbe();')
clamp = 'clampWindowedSizeToDisplay(displayIndex, windowWidth, windowHeight);'
if main.count(clamp) != 1:
    raise RuntimeError('Window-size fixture injection point changed.')
main = main.replace(clamp, '(void)displayIndex;')
main = main.replace('if(shouldPlayIntro && (bFirstInit==true))', 'if(false && shouldPlayIntro && (bFirstInit==true))')
position = main.index('int main(')
main = main[:position] + '#include "' + str(root / 'tests/menu/ui-gallery-probe.inc') + '"\n' + main[position:]
work = Path(tempfile.mkdtemp(prefix='ui-gallery-build-', dir=out))
source, obj = work / 'ui-gallery-main.cpp', work / 'ui-gallery-main.o'
source.write_text(main)
compile_command = shlex.split(next(line for line in commands if ' -c ' in line and '/src/main.cpp' in line))
for option in ('-include', '-MT', '-MF'):
    if option in compile_command:
        index = compile_command.index(option)
        del compile_command[index:index + 2]
for option in ('-MD', '-MMD'):
    if option in compile_command:
        compile_command.remove(option)
compile_command[compile_command.index('-o') + 1] = str(obj)
compile_command[compile_command.index('-c') + 1] = str(source)
compile_command.append('-fno-access-control')
app = work / 'ui-gallery.app/Contents'
(app / 'MacOS').mkdir(parents=True)
(app / 'Resources').symlink_to(build / 'bin/dunecity.app/Contents/Resources')
binary = app / 'MacOS/ui-gallery'
link = shlex.split(next(line for line in commands if ' -o ' + target + ' ' in line))
link = link[link.index('&&') + 1:]
link = link[:link.index('&&')]
link[link.index('-o') + 1] = str(binary)
link = [str(obj) if arg.endswith('/main.cpp.o') else arg for arg in link]
with (work / 'build.log').open('w') as log:
    subprocess.run(['nice', '-n', '10'] + compile_command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(link, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)

lobby_map = root / 'data/maps/multiplayer/2P - 51x31 - 1v1 - Habbanya-Autumn.ini'


def ini(physical, interface, skin):
    width, height = physical
    text = (f'[Video]\nPhysical Width = {width}\nPhysical Height = {height}\nWidth = {width}\nHeight = {height}\n'
            f'Interface Height = {interface}\nFullscreen = false\nFrameLimit = false\nPreferred Zoom Level = 0\n'
            'Scaler = ScaleHD\nMenu Palette = 0\n')
    if skin is not None:
        text += f'UI Skin = {skin}\n'
    return text + ('[General]\nPlay Intro = false\nPlayer Name = Gallery\n[Audio]\nPlay Music = false\nPlay SFX = false\n'
                   '[Network]\nMetaServer = ""\nRelay Endpoint = ""\nDirect Endpoint = ""\n[Discord]\nWebhookUrl = ""\n')


def run_case(label, skin, case, extra_env=None, write_ini=True, skin_in_ini=True):
    physical, interface, logical = CASES[case]
    case_out = out / label / case
    case_out.mkdir(parents=True, exist_ok=True)
    profile = work / ('profile-' + label + '-' + case)
    profile.mkdir(parents=True, exist_ok=True)
    if write_ini:
        (profile / 'Dune City.ini').write_text(ini(physical, interface, skin if skin_in_ini else None))
    env = dict(os.environ, DUNECITY_USERDIR=str(profile), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', MENU_PROBE_OUT=str(case_out), MENU_PROBE_WIDTH=str(logical[0]),
               MENU_PROBE_HEIGHT=str(logical[1]), UI_GALLERY_SKIN=str(skin), UI_GALLERY_MAP=str(lobby_map))
    env.update(extra_env or {})
    logpath = case_out / 'run.log'
    with logpath.open('w') as log:
        try:
            result = subprocess.run(['nice', '-n', '10', str(binary), '--window', '--showlog'], cwd=case_out, env=env,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = logpath.read_text(errors='replace')
    passed = [line for line in text.splitlines() if 'UI_GALLERY_PASS:' in line]
    failed = [line.split('UI_GALLERY_FAIL:', 1)[1].strip() for line in text.splitlines() if 'UI_GALLERY_FAIL:' in line]
    issues = []
    for line in text.splitlines():
        m = re.search(r'UI_GALLERY_ISSUE: screen=(\S+) kind=(\S+) (.*)$', line)
        if m:
            issues.append((label, case, m.group(1), m.group(2), m.group(3)))
    plates = [line.split('UI_GALLERY_PLATE:', 1)[1].strip() for line in text.splitlines() if 'UI_GALLERY_PLATE:' in line]
    # A screen that could not be built or broke an assertion is a failure, not a layout finding;
    # so is any widget over a desert background's corner crest.
    broken = [issue for issue in issues if issue[3] in ('failed', 'over-corner-crest')]
    failed += [f'{issue[2]}: {issue[4]}' for issue in broken]
    ok = code == 0 and len(passed) == 1 and not broken
    return dict(label=label, case=case, ok=ok, code=code, failed=failed, issues=issues, log=logpath, plates=plates)


jobs = []
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
    for skin in args.skins:
        for case in args.cases:
            jobs.append(pool.submit(run_case, SKINS[skin], skin, case))
    # A 1.0.850 profile has no UI Skin key; a brand-new profile has no Dune City.ini at all.
    jobs.append(pool.submit(run_case, 'profile-850', 2, '1440x900-hd',
                            {'UI_GALLERY_FRESH_PROFILE': '1'}, True, False))
    jobs.append(pool.submit(run_case, 'profile-new', 2, '640x480',
                            {'UI_GALLERY_NEW_PROFILE': '1'}, False))
    results = [job.result() for job in jobs]

# Contact sheets: one per skin x case, every screen as a labelled thumbnail.
try:
    from PIL import Image, ImageDraw
    contact = out / 'contact'
    contact.mkdir(exist_ok=True)
    for result in results:
        folder = out / result['label'] / result['case']
        shots = sorted(folder.glob('*.png'))
        if not shots:
            continue
        thumb_w = 480
        thumbs = []
        for shot in shots:
            image = Image.open(shot).convert('RGB')
            scale = thumb_w / image.width
            thumbs.append((shot.stem, image.resize((thumb_w, max(1, int(image.height * scale))), Image.NEAREST)))
        cols = 4
        cell_h = max(t.height for _, t in thumbs) + 18
        rows = (len(thumbs) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * (thumb_w + 8) + 8, rows * (cell_h + 8) + 30), (32, 32, 32))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 8), f"{result['label']} {result['case']} - {len(thumbs)} screens", fill=(255, 255, 255))
        for i, (name, t) in enumerate(thumbs):
            x, y = 8 + (i % cols) * (thumb_w + 8), 30 + (i // cols) * (cell_h + 8)
            draw.text((x, y), name, fill=(255, 220, 120))
            sheet.paste(t, (x, y + 16))
        sheet.save(contact / f"{result['label']}-{result['case']}.png")
except ImportError:
    print('PIL is unavailable: no contact sheets')

all_issues = [issue for result in results for issue in result['issues']]
modern = {(case, screen, kind, re.sub(r'\d+', 'N', detail)) for label, case, screen, kind, detail in all_issues if label == 'modern'}
with (out / 'issues.tsv').open('w') as tsv:
    tsv.write('skin\tcase\tscreen\tkind\tonly_in_desert_skin\tdetail\n')
    for label, case, screen, kind, detail in sorted(all_issues):
        regression = label in ('lapis', 'classic') and (case, screen, kind, re.sub(r'\d+', 'N', detail)) not in modern
        tsv.write(f'{label}\t{case}\t{screen}\t{kind}\t{"yes" if regression else ""}\t{detail}\n')
lines = []
for result in sorted(results, key=lambda r: (r['label'], r['case'])):
    status = 'PASS' if result['ok'] else f"FAIL ({result['code']}; {'; '.join(result['failed']) or 'see ' + str(result['log'])})"
    lines.append(f"{result['label']:12} {result['case']:18} {status} issues={len(result['issues'])}")
(out / 'summary.txt').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))
print(f'issues: {out / "issues.tsv"}; contact sheets: {out / "contact"}')
if not args.keep:
    shutil.rmtree(work, ignore_errors=True)
subprocess.run(['python3', str(root / 'scripts/check-build-deps.py'), str(build)], check=True, cwd=root)
if not all(result['ok'] for result in results):
    raise SystemExit('Failed: ' + ', '.join(f"{r['label']}/{r['case']}" for r in results if not r['ok']))
