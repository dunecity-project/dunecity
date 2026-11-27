"""Did a play style's switch actually do its job in the measured games?

CLAUDE.md, "Asked-for behaviour: an acceptance list first, and the harness refuses untested verdicts",
rule 4 (and "AI behaviours: prove the switch actually does its job before its score counts", rule 3).

A play style that turns on a behaviour declares in tests/ai/activation-events.json the match-log
lines that show it firing and the lines that show it reaching its effect. One line registers a style:

    "turret-drill": {"fire": ["TURRET_FRONT: rule=turret_drill"], "success": ["TURRET_FRONT_ENGAGED: rule=turret_drill"]},

A pattern is a log marker (the text up to and including its ':') followed by optional key=value
filters, all of which must equal the line's own key=value fields. Every such line must name its house
as house=<engine house number>. Several patterns in a list count together.

vanilla-match.py counts, per game and house, the fire and success lines of the style its label plays
(default@<style>, also default@<style>#strategy or /variant) into activation.csv. The reports
(vanilla-match.py --report, play-style-matrix.py, turret-front-report.py) then print each arm's fire
and success rate (share of its house-games with at least one such line), and the verdict becomes
NOT TESTED in place of beats / level with / loses to when the behaviour reached its effect in fewer
than half of the arm's house-games. A label whose style declared no events is marked as such.
"""
import csv
import json
from collections import defaultdict
from pathlib import Path

REGISTRY_PATH = Path(__file__).resolve().with_name('activation-events.json')
FIELDS = ('style', 'fired', 'succeeded')
THRESHOLD = 0.5


def style_of(label):
    """The play style a label plays: default@<style>[#strategy][/variant...] -> <style>, else ''."""
    if '@' not in label:
        return ''
    style = label.split('@', 1)[1]
    for stop in ('#', '/', '+', ','):
        style = style.split(stop, 1)[0]
    return style


def parse_pattern(text):
    """'MARKER: key=value ...' -> ('MARKER:', {key: value})."""
    if ':' not in text:
        raise ValueError(f'Activation pattern without a marker: {text!r}')
    marker, rest = text.split(':', 1)
    marker = marker.strip() + ':'
    filters = dict(part.split('=', 1) for part in rest.split() if '=' in part)
    if any(part for part in rest.split() if '=' not in part):
        raise ValueError(f'Activation pattern filters must be key=value: {text!r}')
    return marker, filters


def load_registry(path=REGISTRY_PATH):
    """{style: {'fire': [(marker, filters)], 'success': [(marker, filters)]}}."""
    if not Path(path).exists():
        return {}
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    registry = {}
    for style, entry in data.items():
        if style.startswith('_'):
            continue
        if set(entry) - {'fire', 'success', 'note'} or not entry.get('fire') or not entry.get('success'):
            raise ValueError(f'Activation entry {style!r} needs "fire" and "success" pattern lists')
        registry[style] = {kind: [parse_pattern(p) for p in entry[kind]] for kind in ('fire', 'success')}
    return registry


def _fields(line, marker):
    return dict(part.split('=', 1) for part in line.split(marker, 1)[1].split() if '=' in part)


def count(lines, styles_by_house, registry):
    """{house number: (fired, succeeded)} for every house whose style is registered."""
    out = {house: [0, 0] for house, style in styles_by_house.items() if style in registry}
    if not out:
        return {}
    wanted = defaultdict(list)   # marker -> [(house, kind, filters)]
    for house in out:
        for kind_index, kind in enumerate(('fire', 'success')):
            for marker, filters in registry[styles_by_house[house]][kind]:
                wanted[marker].append((house, kind_index, filters))
    for line in lines:
        for marker, targets in wanted.items():
            if marker not in line:
                continue
            fields = _fields(line, marker)
            try:
                house = int(fields.get('house', ''))
            except ValueError:
                continue
            for target, kind_index, filters in targets:
                if target == house and all(fields.get(k) == v for k, v in filters.items()):
                    out[house][kind_index] += 1
    return {house: tuple(values) for house, values in out.items()}


class Summary:
    """One arm's activation over its house-games."""

    def __init__(self, label, style, declared, house_games, fired, succeeded):
        self.label, self.style, self.declared = label, style, declared
        self.house_games, self.fired, self.succeeded = house_games, fired, succeeded

    @property
    def fire_rate(self):
        return self.fired / self.house_games if self.house_games else 0.0

    @property
    def success_rate(self):
        return self.succeeded / self.house_games if self.house_games else 0.0

    @property
    def tested(self):
        return self.declared and self.house_games > 0 and self.success_rate >= THRESHOLD

    def text(self):
        if not self.declared:
            return 'no activation events declared'
        return f'fired {self.fire_rate:.0%}, effect {self.success_rate:.0%} of {self.house_games} house-games'


def summarise(house_rows, activation_rows, registry):
    """{label: Summary} for every label that plays a style; house_rows are houses.csv rows of valid
    games (one per house-game), activation_rows the activation.csv rows of the same games."""
    key = lambda r: (r['tag'], r['difficulty'], r['map'], r['seed'], r['sides'], r['house'])  # noqa: E731
    events = {key(r): r for r in activation_rows}
    by_label = defaultdict(lambda: [0, 0, 0])
    for row in house_rows:
        if not style_of(row['label']):
            continue
        counts = by_label[row['label']]
        counts[0] += 1
        event = events.get(key(row))
        if event:
            counts[1] += int(float(event['fired'] or 0) > 0)
            counts[2] += int(float(event['succeeded'] or 0) > 0)
    out = {}
    for label, (games, fired, succeeded) in by_label.items():
        style = style_of(label)
        out[label] = Summary(label, style, style in registry, games, fired, succeeded)
    return out


def verdict(label, text, summaries):
    """The verdict column for \\a label: NOT TESTED when its behaviour did not reach its effect."""
    summary = summaries.get(label)
    if summary is None:
        return text
    if not summary.declared:
        return f'{text} ({summary.text()})'
    if not summary.tested:
        return f'NOT TESTED ({summary.text()})'
    return f'{text} ({summary.text()})'


def table(summaries):
    """Markdown lines: one row per arm that plays a style."""
    out = ['| Arm | Style | House-games | Fired in | Reached its effect in | Tested |', '|---|---|---:|---:|---:|---|']
    for label in sorted(summaries):
        s = summaries[label]
        tested = 'yes' if s.tested else ('NOT TESTED' if s.declared else 'no events declared')
        out.append(f'| {label} | {s.style} | {s.house_games} | {s.fire_rate:.0%} | {s.success_rate:.0%} | {tested} |')
    return out


def read_rows(path):
    path = Path(path)
    if not path.exists():
        return []
    with path.open(newline='') as handle:
        return list(csv.DictReader(handle))
