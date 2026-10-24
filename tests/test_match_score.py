import contextlib
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('match_score', Path(__file__).parents[1] / 'tests/ai/match_score.py')
ms = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ms)

ratio_spec = importlib.util.spec_from_file_location('ratio_analysis', Path(__file__).parent / 'ai/ratio-analysis.py')
ratio = importlib.util.module_from_spec(ratio_spec)
ratio_spec.loader.exec_module(ratio)


def scores(placing):
    return {house: round(p.score, 4) for house, p in placing.items()}


class PlacementScoreTest(unittest.TestCase):
    def test_1v1_win_loss_draw(self):
        win, loss = ms.duel(True, False)
        self.assertEqual((win.score, win.place, win.eliminated, win.drawn), (1.0, 1.0, False, False))
        self.assertEqual((loss.score, loss.place, loss.eliminated, loss.drawn), (-1.0, 2.0, True, False))
        a, b = ms.duel(False, False)
        self.assertEqual((a.score, b.score), (-0.25, -0.25))
        self.assertTrue(a.drawn and b.drawn and not a.eliminated)
        self.assertEqual(a.place, 1.5)
        # The draw score is configurable, between a loss and 0.
        self.assertEqual(ms.duel(False, False, draw_score=-0.5)[0].score, -0.5)
        for bad in (0.0, 0.1, -1.0, -1.5):
            with self.assertRaises(ValueError):
                ms.duel(False, False, draw_score=bad)

    def test_4p_placement_values(self):
        placing = ms.score_game('ABCD', {'D': 100, 'C': 200, 'B': 300})
        self.assertEqual(scores(placing), {'A': 1.0, 'B': 0.3333, 'C': -0.3333, 'D': -1.0})
        self.assertEqual([placing[h].place for h in 'ABCD'], [1, 2, 3, 4])

    def test_8p_top_four_positive_bottom_four_negative(self):
        houses = 'ABCDEFGH'
        placing = ms.score_game(houses, {h: 800 - 100 * i for i, h in enumerate(houses) if i})
        self.assertEqual([round(placing[h].score, 2) for h in houses], [1.0, 0.71, 0.43, 0.14, -0.14, -0.43, -0.71, -1.0])
        self.assertEqual(sum(placing[h].score > 0 for h in houses), 4)
        self.assertAlmostEqual(sum(p.score for p in placing.values()), 0.0)

    def test_ties_share_the_mean_place(self):
        # B and C out at the same time share places 2 and 3; D last.
        placing = ms.score_game('ABCD', {'D': 50, 'B': 300, 'C': 300})
        self.assertEqual((placing['B'].place, placing['C'].place), (2.5, 2.5))
        self.assertEqual(scores(placing), {'A': 1.0, 'B': 0.0, 'C': 0.0, 'D': -1.0})
        # The last two eliminated together: nobody wins, they share first and second.
        both = ms.score_game('AB', {'A': 10, 'B': 10})
        self.assertEqual(scores(both), {'A': 0.0, 'B': 0.0})
        self.assertFalse(both['A'].drawn)

    def test_time_out_with_k_survivors(self):
        # 4P, 2 alive at the limit: share places 1-2 (mean score 0.667) minus 0.25.
        placing = ms.score_game('ABCD', {'C': 900, 'D': 400})
        self.assertEqual(scores(placing), {'A': 0.4167, 'B': 0.4167, 'C': -0.3333, 'D': -1.0})
        self.assertTrue(placing['A'].drawn and not placing['C'].drawn)
        # 4P, all alive: every house the 1v1 draw score.
        self.assertEqual(set(scores(ms.score_game('ABCD', {})).values()), {-0.25})
        # 8P, 7 alive: mean of places 1-7 (0.1429) minus 0.25, still above the eliminated house.
        eight = ms.score_game('ABCDEFGH', {'H': 10})
        self.assertAlmostEqual(eight['A'].score, 1 / 7 - 0.25)
        self.assertEqual(eight['H'].score, -1.0)
        # A survivor never scores below the best eliminated house: 8P, 2 alive, draw score -0.9
        # would give 0.857 - 0.9 = -0.04, below third place (0.43), so it is clamped there.
        clamped = ms.score_game('ABCDEFGH', {h: 100 - i for i, h in enumerate('CDEFGH')}, draw_score=-0.9)
        self.assertAlmostEqual(clamped['A'].score, clamped['C'].score)
        self.assertAlmostEqual(clamped['C'].score, 1 - 4 / 7)
        # And never above an outright win.
        self.assertLessEqual(max(p.score for p in clamped.values()), 1.0)

    def test_teams_rank_teams_and_share_the_score(self):
        teams = {'A': 1, 'B': 1, 'C': 2, 'D': 2, 'E': 3, 'F': 3}
        # Team 1 loses A early but B survives; team 3 is out at 500, team 2 at 700.
        placing = ms.score_game('ABCDEF', {'A': 100, 'E': 400, 'F': 500, 'C': 600, 'D': 700}, teams=teams)
        self.assertEqual(scores(placing), {'A': 1.0, 'B': 1.0, 'C': 0.0, 'D': 0.0, 'E': -1.0, 'F': -1.0})
        self.assertFalse(placing['A'].eliminated)
        # 2v2 time-out: both teams draw.
        draw = ms.score_game('ABCD', {'A': 100}, teams={'A': 1, 'B': 1, 'C': 2, 'D': 2})
        self.assertEqual(set(scores(draw).values()), {-0.25})

    def test_legacy_score_is_kept(self):
        self.assertEqual(ms.legacy_score(True, False, 0, 0, 1000), (1.0, 1.0))
        self.assertEqual(ms.legacy_score(False, True, 0, 0, 1000), (0.0, 0.0))
        self.assertEqual(ms.legacy_score(False, False, 5000, 3000, 1000), (1.0, 0.5))
        self.assertEqual(ms.legacy_score(False, False, 3000, 3500, 1000), (0.5, 0.5))

    def test_decision_rule_is_the_paired_difference_above_zero(self):
        wins = [(1.0, -1.0, False, False)] * 30 + [(-1.0, 1.0, False, False)] * 10 + [(-0.25, -0.25, True, True)] * 20
        c = ms.compare(wins, 1)
        self.assertEqual(c.verdict, 'beats')
        self.assertAlmostEqual(c.difference, 2 * 20 / 60)
        self.assertAlmostEqual(c.candidate_draws, 1 / 3)
        level = ms.compare([(1.0, -1.0, False, False), (-1.0, 1.0, False, False)] * 10, 1)
        self.assertEqual(level.verdict, 'level with')
        # Against a common opponent: drawing less than the control counts.
        fewer = ms.compare([(-0.25, -0.25, True, True)] * 10 + [(1.0, -0.25, False, True)] * 20, 1)
        self.assertEqual(fewer.verdict, 'beats')
        self.assertEqual((fewer.candidate_draws, fewer.control_draws), (1 / 3, 1.0))
        legacy = ms.compare([(0.5, 0.5, True, True)] * 10, 1, 'legacy')
        self.assertEqual((legacy.difference, legacy.verdict), (0.5, 'level with'))

    def test_ratio_paired_gain_keeps_draw_penalty_and_house_position(self):
        games, houses = {}, {}

        def add(seed, candidate, winner, reverse=False, opponent='atreides'):
            mine, other = ('A', 'B') if candidate else ('B', 'A')
            sides = [(opponent, other), ('sardaukar', mine)] if reverse else [('sardaukar', mine), (opponent, other)]
            game = {'tag': 'paired', 'difficulty': 'hard', 'map': 'habbanya-penny',
                    'seed': str(seed), 'sides': '|'.join(f'{h}:{label}' for h, label in sides),
                    'players': '2', 'winner_label': mine if winner == 'mine' else other if winner == 'other' else ''}
            key = tuple(game[k] for k in ratio.KEY)
            games[key] = game
            houses[key] = {h: {'house': h, 'label': label} for h, label in sides}

        add(1, True, 'mine')
        add(1, False, 'other')       # +1 - (-1) = +2, not the doubled contrast gain +4.
        add(2, True, None)
        add(2, False, 'other')       # -0.25 - (-1) = +0.75.
        add(3, True, 'other')
        add(3, False, None)          # -1 - (-0.25) = -0.75.
        add(4, True, 'mine')
        add(4, False, 'other', reverse=True)  # Different positions: not a pair.
        add(5, True, 'mine')         # No control result: not a pair.
        add(6, True, 'mine')
        add(6, False, 'other', opponent='harkonnen')  # Different opponents: not a pair.
        rows = {'paired': []}
        with contextlib.redirect_stdout(io.StringIO()):
            ratio.report_paired(SimpleNamespace(paired='sardaukar', arms='A:B'), games, houses, rows)
        overall = next(r for r in rows['paired'] if r['pairs'] == 'all')
        self.assertEqual(overall['n'], 3)
        self.assertEqual(overall['gain'], '0.667')
        self.assertFalse(any(r['pairs'] == 'second' for r in rows['paired']))

    def test_ratio_paired_gain_reports_unavailable_interval_without_pairs(self):
        text = io.StringIO()
        rows = {'paired': []}
        with contextlib.redirect_stdout(text):
            ratio.report_paired(SimpleNamespace(paired='mercenary', arms='A:B'), {}, {}, rows)
        self.assertIn('No complete pairs; interval unavailable', text.getvalue())
        self.assertEqual(rows['paired'], [])


if __name__ == '__main__':
    unittest.main()
