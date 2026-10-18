#!/usr/bin/env python3
"""QuantBot starting unit ratios: analysis of the randomised, control and tuned arms
(docs/ai-play-styles.md, section 8).

Reads vanilla-match.py result directories (games.csv, houses.csv with start_mix, mix.csv with the
composition per unit-mix slot every 10 game minutes and at the end) and prints:

  --arms A:B[,C:D]   paired placement-score difference of label A over label B (tests/ai/match_score.py:
                     win +1, loss -1, a time-out -0.25 each, so a 1v1 game's difference is +2/-2/0;
                     95% bootstrap interval, A beats B when it is above 0), overall, by house and by
                     map, with each side's time-out (draw) rate
  --random R:C       the randomised arm R (each game a random starting mix within the bounds)
                     against the control C: score by starting share per slot and house, a ridge
                     logistic regression of the result on the starting shares (+ map, opponent),
                     the end-of-game (and 10/20/30-minute) built composition of winners and losers,
                     per-type cost-effectiveness (combat reward per value built, survival), and the
                     candidate tuned mixes derived from them (--candidates DIR writes them as play
                     styles with [Unit Ratios Tuned] keys)

  --csv DIR          also writes the compact CSV tables (per-house records, slot tables, candidates)

Regression and winners/losers use the game result (1 win, 0 loss, 0.5 time-out; no kill-value
adjudication). Pure Python (no numpy): the regression is IRLS on a few dozen columns.
"""
import argparse
import csv
import math
import random
import statistics
from collections import defaultdict
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_score  # noqa: E402

SLOTS = ('tank', 'siege', 'launcher', 'special', 'ornithopter', 'trike', 'raider', 'quad')
HOUSES = ('atreides', 'harkonnen', 'ordos')
KEY = ('tag', 'difficulty', 'map', 'seed', 'sides')
LO, HI = 0.02, 0.50


def read(path):
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))


def number(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def load(dirs, tags):
    games, houses, mixes = {}, defaultdict(dict), defaultdict(dict)
    for d in dirs:
        d = Path(d)
        for g in read(d / 'games.csv'):
            if (tags and g['tag'] not in tags) or g['error']:
                continue
            games[tuple(g[k] for k in KEY)] = g
        for h in read(d / 'houses.csv'):
            houses[tuple(h[k] for k in KEY)][h['house']] = h
        for m in read(d / 'mix.csv'):
            key = tuple(m[k] for k in KEY)
            # The final sample, and one per 10-minute mark (a game ending on a mark logs both).
            slot = 'final' if m['final'] == '1' else int(m['minute'])
            mixes[key][(m['house'], slot)] = m
    return games, houses, mixes


def sides(game):
    return [tuple(s.split(':', 1)) for s in game['sides'].split('|')]


def score(game, mine):
    """The game result from mine's side: 1 win, 0 loss, 0.5 time-out."""
    if game['winner_label']:
        return 1.0 if game['winner_label'] == mine['label'] else 0.0
    return 0.5


def delta(game, mine, theirs):
    """(paired placement-score difference mine - theirs, mine drawn) of a 1v1 game."""
    a, b = match_score.duel(game['winner_label'] == mine['label'], game['winner_label'] == theirs['label'])
    return a.score - b.score, a.drawn


def bootstrap(values, rounds=2000):
    if not values:
        return (0.0, 0.0)
    rng = random.Random(20261018)
    means = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(rounds))
    return means[int(0.025 * rounds)], means[int(0.975 * rounds) - 1]


def fmt(values):
    if not values:
        return '-'
    low, high = bootstrap(values)
    mean = statistics.mean(values)
    bold = '**' if low > 0.5 or high < 0.5 else ''
    return f'{bold}{mean:.3f}{bold} ({low:.2f}-{high:.2f}) n={len(values)}'


def fmt_delta(values):
    """Mean paired difference, 95% interval; bold when the interval excludes 0."""
    if not values:
        return '-'
    low, high = bootstrap(values)
    mean = statistics.mean(values)
    bold = '**' if low > 0 or high < 0 else ''
    return f'{bold}{mean:+.3f}{bold} ({low:+.2f}..{high:+.2f}) n={len(values)}'


def records(games, houses, mixes, x, y):
    """One record per 1v1 game between labels x and y, from x's side."""
    out = []
    for key, game in games.items():
        labels = [l for _, l in sides(game)]
        if game['players'] != '2' or sorted(labels) != sorted([x, y]) or x == y:
            continue
        rows = {h['label']: h for h in houses[key].values()}
        if x not in rows or y not in rows:
            continue
        mine, theirs = rows[x], rows[y]
        rec = {'key': key, 'map': game['map'], 'seed': game['seed'], 'house': mine['house'],
               'opponent': theirs['house'], 'score': score(game, mine), 'end_s': number(game['end_s']),
               'start': [int(v) for v in mine.get('start_mix', '').split(',')] if mine.get('start_mix') else None,
               'opp_start': [int(v) for v in theirs.get('start_mix', '').split(',')] if theirs.get('start_mix') else None}
        rec['delta'], rec['drawn'] = delta(game, mine, theirs)
        for side, row in (('mine', mine), ('theirs', theirs)):
            for mark in (10, 20, 30, 'final'):
                sample = mixes[key].get((row['house'], mark))
                if sample:
                    rec[(side, mark)] = {f: [number(v) for v in sample[f].split('/')] for f in ('built', 'lost', 'army', 'reward')}
        out.append(rec)
    return out


def shares(values, slots=8):
    total = sum(values[:slots])
    return [v / total for v in values[:slots]] if total > 0 else None


# ---- Bounds (the Python mirror of QuantBotStrategy::bound, in fractions) ------------------------------

def bound(weights, fieldable):
    """Clamp-and-renormalise into [2%, 50%] over the fieldable slots (one scale t, bisection)."""
    idx = [i for i in range(8) if fieldable[i]]
    out = [0.0] * 8
    if len(idx) == 1:
        out[idx[0]] = 1.0
        return out
    w = [max(0.0, weights[i]) for i in idx]
    if sum(w) <= 0:
        w = [1.0] * len(idx)
    f = lambda t: sum(min(HI, max(LO, t * v)) for v in w)
    lo, hi = 0.0, 1.0
    while f(hi) < 1.0 and hi < 1e12:
        hi *= 2
    if f(hi) < 1.0:   # every weighted slot at the cap is short: the zero-weight slots share the rest
        zeros = [k for k, v in enumerate(w) if v == 0]
        vals = [HI if v > 0 else 0.0 for v in w]
        rest = 1.0 - sum(vals)
        for k in zeros:
            vals[k] = rest / len(zeros)
    else:
        for _ in range(200):
            mid = (lo + hi) / 2
            lo, hi = (mid, hi) if f(mid) < 1.0 else (lo, mid)
        vals = [min(HI, max(LO, hi * v)) for v in w]
        total = sum(vals)
        vals = [v / total for v in vals]
    for k, i in enumerate(idx):
        out[i] = vals[k]
    return out


def percent(mix, fieldable):
    """Integer percent within the bounds summing to 100 (largest remainders)."""
    raw = [mix[i] * 100 if fieldable[i] else 0.0 for i in range(8)]
    ints = [int(math.floor(v)) for v in raw]
    for i in range(8):
        if fieldable[i]:
            ints[i] = min(50, max(2, ints[i]))
    order = sorted((i for i in range(8) if fieldable[i]), key=lambda i: -(raw[i] - math.floor(raw[i])))
    while sum(ints) < 100:
        for i in order:
            if sum(ints) < 100 and ints[i] < 50:
                ints[i] += 1
    while sum(ints) > 100:
        for i in sorted(order, key=lambda i: -ints[i]):
            if sum(ints) > 100 and ints[i] > 2:
                ints[i] -= 1
    return ints


def uniform_bounded(rng, fieldable):
    idx = [i for i in range(8) if fieldable[i]]
    while True:
        cuts = sorted(rng.random() for _ in range(len(idx) - 1))
        parts = [b - a for a, b in zip([0.0] + cuts, cuts + [1.0])]
        if all(LO <= p <= HI for p in parts):
            out = [0.0] * 8
            for k, i in enumerate(idx):
                out[i] = parts[k]
            return out


# ---- Ridge logistic regression (IRLS) ------------------------------------------------------------------

def solve(a, b):
    n = len(a)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        if abs(m[c][c]) < 1e-12:
            continue
        for r in range(n):
            if r != c:
                f = m[r][c] / m[c][c]
                for k in range(c, n + 1):
                    m[r][k] -= f * m[c][k]
    return [m[i][n] / m[i][i] if abs(m[i][i]) > 1e-12 else 0.0 for i in range(n)]


def inverse(a):
    n = len(a)
    cols = []
    for j in range(n):
        e = [0.0] * n
        e[j] = 1.0
        cols.append(solve(a, e))
    return [[cols[j][i] for j in range(n)] for i in range(n)]


def logistic(xs, ys, ridge=1.0, penalised=None):
    p = len(xs[0])
    beta = [0.0] * p
    penalised = penalised or [True] * p
    for _ in range(50):
        h = [[ridge if (i == j and penalised[i]) else 0.0 for j in range(p)] for i in range(p)]
        g = [-(ridge * beta[i]) if penalised[i] else 0.0 for i in range(p)]
        for x, y in zip(xs, ys):
            z = sum(b * v for b, v in zip(beta, x))
            mu = 1 / (1 + math.exp(-max(-30, min(30, z))))
            w = mu * (1 - mu)
            for i in range(p):
                g[i] += (y - mu) * x[i]
                if x[i]:
                    for j in range(p):
                        h[i][j] += w * x[i] * x[j]
        step = solve(h, g)
        beta = [b + s for b, s in zip(beta, step)]
        if max(abs(s) for s in step) < 1e-8:
            break
    se = [math.sqrt(max(0.0, v)) for v in (inverse(h)[i][i] for i in range(p))]
    return beta, se


# ---- Report ----------------------------------------------------------------------------------------

def report_arms(args, games, houses):
    print('\n### Arms (paired placement-score difference A - B; time-outs -0.25 each)\n')
    print('| A v B | Δ all | Atreides | Harkonnen | Ordos | W-L / time-outs (draw rate) |')
    print('|---|---|---|---|---|---|')
    for pair in args.arms.split(','):
        x, y = pair.split(':')
        recs = records(games, houses, defaultdict(dict), x, y)
        if not recs:
            continue
        by = defaultdict(list)
        for r in recs:
            by[r['house']].append(r['delta'])
        wins = sum(1 for r in recs if r['score'] == 1.0)
        losses = sum(1 for r in recs if r['score'] == 0.0)
        draws = len(recs) - wins - losses
        print(f'| {x} v {y} | {fmt_delta([r["delta"] for r in recs])} | {fmt_delta(by["atreides"])} | '
              f'{fmt_delta(by["harkonnen"])} | {fmt_delta(by["ordos"])} | {wins}-{losses} / {draws} '
              f'({100 * draws / len(recs):.1f}%) |')
    # The house columns mix the house with its opponent's strength. Every house pair is played with
    # both assignments, so the per-house effect of A over B is identified: with s(h, o) the logit of
    # A's score as house h against B's house o, s(h, o) + s(o, h) = d(h) + d(o) for the logit gain
    # d(h) of playing A instead of B as house h. Shown as the score A would get as h against a B of
    # equal strength (0.5 + effect).
    print('\nPer-house effect of A over B (d = logit gain; score against an equal B; from both assignments):\n')
    print('| A v B | Atreides | Harkonnen | Ordos |')
    print('|---|---|---|---|')
    for pair in args.arms.split(','):
        x, y = pair.split(':')
        cells = defaultdict(list)
        for r in records(games, houses, defaultdict(dict), x, y):
            cells[(r['house'], r['opponent'])].append(r['score'])
        logit = lambda h, o: math.log(min(0.99, max(0.01, statistics.mean(cells[(h, o)])))
                                      / (1 - min(0.99, max(0.01, statistics.mean(cells[(h, o)])))))
        pairs = (('atreides', 'harkonnen'), ('ordos', 'atreides'), ('harkonnen', 'ordos'))
        if any(not cells[(h, o)] or not cells[(o, h)] for h, o in pairs):
            continue
        ah, oa, ho = (logit(h, o) + logit(o, h) for h, o in pairs)
        d = {'atreides': (ah + oa - ho) / 2, 'harkonnen': (ah + ho - oa) / 2, 'ordos': (oa + ho - ah) / 2}
        print(f'| {x} v {y} | ' + ' | '.join(f'd={d[h]:+.2f} ({1 / (1 + math.exp(-d[h])):.2f})' for h in HOUSES) + ' |')
    if args.by_map:
        print('\n| A v B | Map | Δ |')
        print('|---|---|---|')
        for pair in args.arms.split(','):
            x, y = pair.split(':')
            groups = defaultdict(list)
            for r in records(games, houses, defaultdict(dict), x, y):
                groups[r['map']].append(r['delta'])
            for name, values in sorted(groups.items()):
                print(f'| {x} v {y} | {name} | {fmt_delta(values)} |')


def fieldable_of(recs):
    out = {}
    for house in HOUSES:
        starts = [r['start'] for r in recs if r['house'] == house and r['start']]
        out[house] = [any(s[i] > 0 for s in starts) for i in range(8)] if starts else [False] * 8
    return out


def composition(recs, side, mark, field='built', who=None):
    """Mean share per slot of `field` at `mark` over records (who: 'win'/'loss'/None)."""
    vals = []
    for r in recs:
        sample = r.get((side, mark))
        if not sample:
            continue
        mine_won = r['score'] == 1.0
        side_won = mine_won if side == 'mine' else r['score'] == 0.0
        if who == 'win' and not side_won:
            continue
        if who == 'loss' and side_won:
            continue
        s = shares(sample[field])
        if s:
            vals.append(s)
    if not vals:
        return None, 0
    return [statistics.mean(v[i] for v in vals) for i in range(8)], len(vals)


def pct(mix, fieldable=None):
    if mix is None:
        return '-'
    return '/'.join('-' if fieldable and not fieldable[i] else f'{100 * mix[i]:.0f}' for i in range(8))


def report_random(args, games, houses, mixes, out_rows):
    x, y = args.random.split(':')
    recs = [r for r in records(games, houses, mixes, x, y) if r['start']]
    ctrl = records(games, houses, mixes, y, x)
    fieldable = fieldable_of(recs)
    print(f'\n### Randomised arm {x} v {y}: {len(recs)} games\n')
    print(f'Overall {fmt([r["score"] for r in recs])}')
    candidates = {}
    rng = random.Random(7)
    for house in HOUSES:
        hr = [r for r in recs if r['house'] == house]
        if not hr:
            continue
        f = fieldable[house]
        idx = [i for i in range(8) if f[i]]
        print(f'\n#### {house.capitalize()} (randomised side): {fmt([r["score"] for r in hr])}; '
              f'fieldable: {", ".join(SLOTS[i] for i in idx)}\n')
        # Score by starting share bucket.
        buckets = ((0.02, 0.08), (0.08, 0.16), (0.16, 0.25), (0.25, 0.51))
        print('| Slot | ' + ' | '.join(f'start {int(100 * a)}-{int(100 * b) if b < 0.51 else 50}%' for a, b in buckets) + ' | slope |')
        print('|---|' + '---|' * (len(buckets) + 1))
        for i in idx:
            cells = []
            for a, b in buckets:
                vals = [r['score'] for r in hr if a <= r['start'][i] / 10000 < b]
                cells.append(f'{statistics.mean(vals):.2f} n={len(vals)}' if vals else '-')
            xs = [r['start'][i] / 10000 for r in hr]
            ys = [r['score'] for r in hr]
            mx, my = statistics.mean(xs), statistics.mean(ys)
            sxx = sum((v - mx) ** 2 for v in xs)
            slope = sum((a - mx) * (b - my) for a, b in zip(xs, ys)) / sxx if sxx else 0
            print(f'| {SLOTS[i]} | ' + ' | '.join(cells) + f' | {slope:+.2f} per 100% |')
            out_rows['slots'].append({'house': house, 'slot': SLOTS[i], **{f'b{k}': c for k, c in enumerate(cells)},
                                      'slope_per_100pct': f'{slope:.3f}', 'n': len(hr)})
        # Ridge logistic regression: log-ratio-free linear shares (reference slot dropped), map and opponent.
        ref = max(idx, key=lambda i: statistics.mean(r['start'][i] for r in hr)) if False else idx[0]
        cols = [i for i in idx if i != ref]
        maps = sorted({r['map'] for r in recs})
        opps = sorted({r['opponent'] for r in hr})

        def design(start, mp, opp, quad=False):
            row = [1.0] + [start[i] for i in cols]
            if quad:
                row += [start[i] ** 2 for i in idx]
            row += [1.0 if mp == m else 0.0 for m in maps[1:]] + [1.0 if opp == o else 0.0 for o in opps[1:]]
            return row
        xs = [design([v / 10000 for v in r['start']], r['map'], r['opponent']) for r in hr]
        ys = [r['score'] for r in hr]
        beta, se = logistic(xs, ys, ridge=1.0, penalised=[False] + [True] * (len(xs[0]) - 1))
        print(f'\nLogistic regression (ridge 1; shares in fractions, reference {SLOTS[ref]}; map and opponent '
              f'dummies): ' + ', '.join(f'{SLOTS[c]} {beta[k + 1]:+.2f}±{se[k + 1]:.2f}' for k, c in enumerate(cols)))
        for k, c in enumerate(cols):
            out_rows['regression'].append({'house': house, 'slot': SLOTS[c], 'reference': SLOTS[ref],
                                           'coef_per_unit_share': f'{beta[k + 1]:.3f}', 'se': f'{se[k + 1]:.3f}', 'n': len(hr)})
        # Quadratic model, maximised over random bounded mixes: the mean of the best 5% (regression candidate).
        xq = [design([v / 10000 for v in r['start']], r['map'], r['opponent'], quad=True) for r in hr]
        bq, _ = logistic(xq, ys, ridge=2.0, penalised=[False] + [True] * (len(xq[0]) - 1))
        pool = [uniform_bounded(rng, f) for _ in range(4000)]
        nq = 1 + len(cols) + len(idx)

        def predict(mix):
            row = [1.0] + [mix[i] for i in cols] + [mix[i] ** 2 for i in idx]
            return sum(b * v for b, v in zip(bq[:nq], row))
        pool.sort(key=predict, reverse=True)
        top = pool[:200]
        regression = bound([statistics.mean(m[i] for m in top) for i in range(8)], f)
        # Gradient: from the mean draw a step along the linear model's coefficients (centred over
        # the fieldable slots, so the shares still sum to 1), the largest move 12 points.
        mean_start = [statistics.mean(r['start'][i] / 10000 for r in hr) for i in range(8)]
        coef = {ref: 0.0, **{c: beta[k + 1] for k, c in enumerate(cols)}}
        centre = statistics.mean(coef[i] for i in idx)
        direction = {i: coef[i] - centre for i in idx}
        scale = 0.12 / max(1e-9, max(abs(v) for v in direction.values()))
        gradient = bound([mean_start[i] + scale * direction[i] if f[i] else 0.0 for i in range(8)], f)
        # Winners' end-of-game production (both arms, this house), and per-type cost-effectiveness.
        both = hr + [r for r in ctrl if r['house'] == house]
        end_win, n_win = composition(both, 'mine', 'final', 'built', 'win')
        end_loss, n_loss = composition(both, 'mine', 'final', 'built', 'loss')
        winners_end = bound(end_win, f) if end_win else None
        reward = [0.0] * 9
        built = [0.0] * 9
        lost = [0.0] * 9
        for r in both:
            s = r.get(('mine', 'final'))
            if s:
                for i in range(9):
                    reward[i] += s['reward'][i]
                    built[i] += s['built'][i]
                    lost[i] += s['lost'][i]
        eff = [reward[i] / built[i] if built[i] > 0 else 0.0 for i in range(8)]
        cost = bound(eff, f)
        print(f'\nEnd of game, built value share (this house, both arms): winners {pct(end_win, f)} (n={n_win}), '
              f'losers {pct(end_loss, f)} (n={n_loss})')
        for mark in (10, 20, 30):
            w, nw = composition(both, 'mine', mark, 'built', 'win')
            l, nl = composition(both, 'mine', mark, 'built', 'loss')
            print(f'Minute {mark}: winners {pct(w, f)} (n={nw}), losers {pct(l, f)} (n={nl})')
        print('Per type (both arms): reward per value built ' + ', '.join(
            f'{SLOTS[i]} {eff[i]:.2f}' for i in idx) + '; survival ' + ', '.join(
            f'{SLOTS[i]} {1 - lost[i] / built[i]:.2f}' for i in idx if built[i] > 0))
        for i in list(idx) + [8]:
            name = SLOTS[i] if i < 8 else 'infantry'
            out_rows['types'].append({'house': house, 'slot': name, 'built_value': int(built[i]), 'lost_value': int(lost[i]),
                                      'reward': int(reward[i]), 'reward_per_built': f'{reward[i] / built[i]:.3f}' if built[i] else '',
                                      'survival': f'{1 - lost[i] / built[i]:.3f}' if built[i] else '',
                                      'end_share_winners': f'{end_win[i]:.3f}' if end_win and i < 8 else '',
                                      'end_share_losers': f'{end_loss[i]:.3f}' if end_loss and i < 8 else ''})
        candidates[house] = {'regression': regression, 'gradient': gradient, 'winners_end': winners_end, 'cost': cost}
        print('\nCandidates (percent, tank/siege/launcher/special/ornithopter/trike/raider/quad):')
        for name, mix in candidates[house].items():
            if mix:
                ints = percent(mix, f)
                print(f'- {name}: {",".join(map(str, ints))}')
                out_rows['candidates'].append({'house': house, 'candidate': name, 'ratios': ','.join(map(str, ints))})
    return candidates, fieldable


def write_candidates(directory, candidates, fieldable):
    names = sorted({name for c in candidates.values() for name in c})
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    for name in names:
        lines = [f'# Test-only tuned starting mix "{name}" (tests/ai/ratio-analysis.py; docs/ai-play-styles.md,',
                 '# section 8). Label: default@<file>#Tuned-Swarm. Percent: tank, siege, launcher, special,',
                 '# ornithopter, trike, raider trike, quad.', '', '[Unit Ratios Tuned]']
        for house, c in candidates.items():
            if c.get(name):
                lines.append(f'{house.capitalize()}={",".join(map(str, percent(c[name], fieldable[house])))}')
        (directory / f'tuned-{name.replace("_", "-")}.ini').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('results', nargs='+', help='vanilla-match.py result directories')
    parser.add_argument('--tags', nargs='*')
    parser.add_argument('--arms')
    parser.add_argument('--by-map', action='store_true')
    parser.add_argument('--random')
    parser.add_argument('--candidates', help='Write the candidates as play styles into this directory')
    parser.add_argument('--csv', help='Write the compact tables into this directory')
    args = parser.parse_args()
    games, houses, mixes = load(args.results, set(args.tags or []))
    out_rows = defaultdict(list)
    if args.arms:
        report_arms(args, games, houses)
    if args.random:
        candidates, fieldable = report_random(args, games, houses, mixes, out_rows)
        if args.candidates:
            write_candidates(args.candidates, candidates, fieldable)
        if args.csv:
            x, y = args.random.split(':')
            d = Path(args.csv)
            d.mkdir(parents=True, exist_ok=True)
            recs = [r for r in records(games, houses, mixes, x, y) if r['start']]
            with (d / 'random-games.csv').open('w', newline='') as handle:
                w = csv.writer(handle)
                w.writerow(['map', 'seed', 'house', 'opponent', 'score', 'end_min'] + [f'start_{s}' for s in SLOTS]
                           + [f'end_built_{s}' for s in SLOTS] + [f'opp_end_built_{s}' for s in SLOTS])
                for r in recs:
                    mine = shares(r[('mine', 'final')]['built']) if ('mine', 'final') in r else None
                    theirs = shares(r[('theirs', 'final')]['built']) if ('theirs', 'final') in r else None
                    w.writerow([r['map'], r['seed'], r['house'], r['opponent'], r['score'], f'{r["end_s"] / 60:.1f}']
                               + r['start'] + [f'{v:.3f}' for v in (mine or [0] * 8)] + [f'{v:.3f}' for v in (theirs or [0] * 8)])
            for name, rows in out_rows.items():
                if rows:
                    with (d / f'{name}.csv').open('w', newline='') as handle:
                        w = csv.DictWriter(handle, fieldnames=list(rows[0]))
                        w.writeheader()
                        w.writerows(rows)


if __name__ == '__main__':
    main()
