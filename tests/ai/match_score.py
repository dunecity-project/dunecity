"""Placement scoring of AI test games (docs/ai-play-styles.md, "Scoring and the default rule").

Every report of formation-duel.py, vanilla-match.py and play-style-matrix.py scores games here.

- Placement score of place p among N entrants (houses, or teams in team games):
  1 - 2(p - 1)/(N - 1): first +1, last -1, linear in between (1v1: win +1, loss -1).
- Places: the last entrant standing is first; eliminated entrants are ranked by elimination time
  (later = better); entrants eliminated at the same time share the mean of their places.
- Time-out (two or more entrants alive at the time limit): nobody wins. The k survivors share
  places 1..k and score the mean of those places' scores minus the draw penalty D = -draw_score,
  clamped to [score of place k+1 (the best eliminated entrant; a loss if nobody was eliminated), 1].
  draw_score is the score of a 1v1 draw, -0.25 by default: negative, but better than a loss.
- Teams: an entrant is a team; it is eliminated when all its houses are, at the last of their
  elimination times; every house of the team gets the team's place and score.

The default rule compares a candidate with the control on the per-game difference of their mean
placement scores (paired by game, or by the setup the harness pairs), with a 95% bootstrap interval:
the candidate beats the control when the whole interval is above 0.

--scoring legacy restores the old rule (1 win, 0 loss, a time-out 1/0 to the side ahead on killed
unit value by --adjudicate credits, else 0.5; the candidate beats the control when the interval of
its mean score is above 0.5) for comparing old reports.
"""
import csv
import random
import statistics
from dataclasses import dataclass

DRAW_SCORE = -0.25
SCORINGS = ('placement', 'legacy')


def placement_score(place, entrants):
    """Score of a (possibly shared, fractional) place among 'entrants' entrants."""
    if entrants < 2:
        raise ValueError('a scored game needs at least two entrants')
    return 1.0 - 2.0 * (place - 1.0) / (entrants - 1.0)


def check_draw_score(value):
    """argparse type for --draw-score: a 1v1 draw must be negative and better than a loss."""
    value = float(value)
    if not -1.0 < value < 0.0:
        raise ValueError(f'draw score {value} must be above -1 (a loss) and below 0')
    return value


@dataclass(frozen=True)
class Placing:
    place: float       # 1 = first; shared places are the mean of the places covered
    score: float       # placement score, with the draw penalty for a time-out survivor
    eliminated: bool   # the entrant (the house's team) was eliminated
    drawn: bool        # the entrant was alive at the time limit of a time-out (the draw penalty applied)


def score_game(houses, eliminated, draw_score=DRAW_SCORE, teams=None):
    """Placing per house.

    houses: the houses (any hashable ids) of the game; eliminated: {house: elimination time} for the
    houses eliminated (absent = alive at the end); teams: optional {house: team}, default every house
    its own team. Two or more teams alive at the end is a time-out."""
    check_draw_score(draw_score)
    team_of = {house: (teams or {}).get(house, house) for house in houses}
    members = {}
    for house, team in team_of.items():
        members.setdefault(team, []).append(house)
    entrants = len(members)
    out_time = {team: max(eliminated[h] for h in hs) for team, hs in members.items()
                if all(h in eliminated for h in hs)}
    survivors = [team for team in members if team not in out_time]
    k = len(survivors)
    placing = {}
    if k == 1:
        placing[survivors[0]] = Placing(1.0, 1.0, False, False)
    elif k >= 2:
        shared = statistics.mean(range(1, k + 1))
        floor = placement_score(k + 1, entrants) if k < entrants else -1.0
        score = min(1.0, max(floor, placement_score(shared, entrants) + draw_score))
        for team in survivors:
            placing[team] = Placing(shared, score, False, True)
    # Eliminated teams, last out first; a tie at the same time shares the mean of its places.
    times = sorted(set(out_time.values()), reverse=True)
    next_place = k + 1
    for time in times:
        group = [team for team, t in out_time.items() if t == time]
        shared = statistics.mean(range(next_place, next_place + len(group)))
        for team in group:
            placing[team] = Placing(shared, placement_score(shared, entrants), True, False)
        next_place += len(group)
    return {house: placing[team] for house, team in team_of.items()}


def duel(winner_is_a, winner_is_b, draw_score=DRAW_SCORE):
    """(a's Placing, b's Placing) of a 1v1 from its result (neither = time-out)."""
    placing = score_game(('a', 'b'), {'b': 0} if winner_is_a else {'a': 0} if winner_is_b else {}, draw_score)
    return placing['a'], placing['b']


def legacy_score(won, lost, kill_me, kill_them, margin):
    """The old score and strict score: 1 win, 0 loss; a time-out goes to the side 'margin'+ credits
    ahead on killed unit value, else 0.5 (strict: always 0.5)."""
    if won:
        return 1.0, 1.0
    if lost:
        return 0.0, 0.0
    trade = kill_me - kill_them
    return (1.0 if trade >= margin else 0.0 if trade <= -margin else 0.5), 0.5


def bootstrap(values, seed, rounds=2000):
    """95% interval of the mean, resampling the values (fixed seed: reports are reproducible)."""
    if not values:
        return (0.0, 0.0)
    rng = random.Random(seed)
    means = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(rounds))
    return means[int(0.025 * rounds)], means[int(0.975 * rounds) - 1]


@dataclass(frozen=True)
class Comparison:
    n: int
    candidate: float       # mean score of the candidate
    control: float         # mean score of the control
    difference: float      # placement: mean paired difference; legacy: the candidate's mean score
    low: float             # 95% interval of 'difference'
    high: float
    candidate_draws: float  # share of the candidate's games ending with it alive at a time-out
    control_draws: float
    verdict: str           # 'beats', 'loses to' or 'level with'


def compare(pairs, seed, scoring='placement'):
    """The default rule over (candidate score, control score, candidate drawn, control drawn) pairs.

    placement: the interval of the mean paired difference must lie wholly above (beats) or below
    (loses to) 0. legacy: the interval of the candidate's mean score against 0.5."""
    if not pairs:
        return None
    candidate = [p[0] for p in pairs]
    control = [p[1] for p in pairs]
    if scoring == 'legacy':
        values, line = candidate, 0.5
    else:
        values, line = [a - b for a, b in zip(candidate, control)], 0.0
    low, high = bootstrap(values, seed)
    verdict = 'beats' if low > line else 'loses to' if high < line else 'level with'
    return Comparison(len(pairs), statistics.mean(candidate), statistics.mean(control), statistics.mean(values),
                      low, high, statistics.mean(1.0 if p[2] else 0.0 for p in pairs),
                      statistics.mean(1.0 if p[3] else 0.0 for p in pairs), verdict)


def significant(comparison):
    return comparison is not None and comparison.verdict != 'level with'


def describe(scoring, draw_score, margin):
    """One line naming the scoring rule, for report headers."""
    if scoring == 'legacy':
        return (f'legacy scoring: 1 win, 0 loss, a time-out to the side {margin:.0f}+ credits ahead on killed value, '
                f'else 0.5; verdict = 95% interval of the score against 0.5')
    return (f'placement scoring: 1st +1 ... last -1, time-out survivors share the top places minus '
            f'{-draw_score:.2f} (1v1 draw {draw_score:+.2f}); verdict = 95% interval of the paired difference '
            f'against the control above / below 0')


def add_arguments(parser):
    """The report options every harness shares (--adjudicate stays each harness's own, legacy only)."""
    parser.add_argument('--scoring', choices=SCORINGS, default='placement',
                        help='Report: placement (default) or legacy (1/0, adjudicated time-outs, 0.5 line) scoring')
    parser.add_argument('--draw-score', type=check_draw_score, default=DRAW_SCORE,
                        help='Report: the score of a 1v1 draw (time-out); between -1 and 0, default -0.25')
    parser.add_argument('--scores', metavar='CSV',
                        help='Report: write every game\'s sides (eliminated, place, score, drawn, game length, '
                             'decisive) to this CSV')


SCORE_FIELDS = ('game', 'side', 'house', 'label', 'eliminated', 'eliminated_s', 'place', 'score', 'drawn',
                'end_s', 'decisive', 'kill_value', 'loss_value')


def write_scores(path, rows):
    """Per game and side: eliminated, place, score, game length and decisive (ended before the limit)."""
    with open(path, 'w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=SCORE_FIELDS, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)
