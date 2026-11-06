#!/usr/bin/env python3
"""Historical native league cluster parity dispatcher, NOT a backport of formation gameplay.

The queue unconditionally requests this filename and box:none/128/7101. Release
807 predates formations and the Random Skirmish generator. These two fixtures
instead exercise its real standalone QuantBots on one frozen 128x128 DuneCity
map (generator seed 9101), game seed 7101, both roster orders. Native periodic
GameStateDigest and HISTORICAL_RESULT lines must agree with the mini exactly.
The shape labels are accepted only as that dispatcher protocol; no result is
reported as a historical formation duel. See docs/ai-baseline-gate.md.
"""
import argparse
import importlib.util
from pathlib import Path

support_path = Path(__file__).with_name('ai-baseline-league.py')
spec = importlib.util.spec_from_file_location('ai_baseline_bridge', support_path)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)
read = bridge.read
scratch_dir = bridge.scratch_dir
finish_game = bridge.finish_game


def schedule(args):
    if args.pairs != 'box:none' or args.sizes != [128] or args.seeds != [7101]:
        raise ValueError('Only the documented historical cluster parity dispatcher is supported')
    games = bridge.fixture_games()
    source = next(game for game in games if game['mod'] == 'dunecity' and game['seed'] == 9101
                  and '/none/1v1/medium/' in game['id'])
    result = []
    for order in range(2):
        game = dict(source)
        sides = source['sides'].split(',')
        if order:
            sides.reverse()
        game.update(id='historical-parity/frozen-map9101/game7101/order%d' % order,
                    seed=7101, difficulty=args.difficulty, minutes=args.minutes,
                    sides=','.join(side.rsplit(':', 1)[0] + ':qBot' + args.difficulty.title() for side in sides),
                    reference_research_cells=['unsupported'])
        result.append(game)
    return result


def build_probe(args, scratch):
    return bridge.build_probe(args, scratch)


def play(args, scratch, binary, game):
    # cluster_unit replaces this namespace's finish_game to capture parity logs
    # and retain node-owned scratch; propagate that hook to the implementation.
    bridge.finish_game = finish_game
    return bridge.play(args, scratch, binary, game)


def report(args):
    print('Exact-map native historical parity, not historical formations'); return 0


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--results', required=True)
parser.add_argument('--build-dir', type=Path, default=bridge.root / 'build')
parser.add_argument('--tag', default='parity')
parser.add_argument('--difficulty', choices=('medium', 'hard', 'brutal'), default='hard')
parser.add_argument('--pairs', default='box:none')
parser.add_argument('--sizes', type=int, nargs='+', default=[128])
parser.add_argument('--seeds', type=int, nargs='+', default=[7101])
parser.add_argument('--minutes', type=int, default=12)
parser.add_argument('--wall-timeout', type=int, default=900)
parser.add_argument('--game-options', type=Path,
                    help='Frozen override; bytes must match the source fixture mod')
parser.add_argument('--capture-mib', type=int, default=256)
args = parser.parse_args() if __name__ == '__main__' else None
if args is not None:
    raise SystemExit('Historical parity is dispatched only by the cluster; no formation gameplay was backported')
