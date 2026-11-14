"""Authored campaign adapter for the existing experiment runner. No default changes.

Each game remains an observation; missing evidence is never a zero or a win.
Reports bind reviews to the exact before/after records. Historical comparisons
are descriptive whole-version comparisons, never competitive head-to-head proof.
"""
import argparse
import collections
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import re

TIERS = ('easy', 'medium', 'hard', 'brutal')
HOUSES = {'harkonnen': '0', 'atreides': '1', 'ordos': '2'}
ALL_HOUSES = dict(HOUSES, fremen='3', sardaukar='4', mercenary='5')
IDENTITY = ('seed', 'mod', 'map', 'sides', 'limit', 'campaign_mode', 'partner', 'enemy_ai', 'enemy_difficulty')
SETUP = ('map_hash', 'options_hash', 'content_hash', 'object_data_hash', 'observing')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def level(mission):
    return 1 if mission == 1 else 9 if mission == 22 else (mission + 4) // 3


def cases(path):
    from research_report import compact_json
    deck = compact_json(path)
    require(deck.get('schema') == 'campaign-research-cases/1', 'Expected campaign research deck')
    rows = deck.get('cases', [])
    require(isinstance(rows, list) and rows, 'Empty campaign deck')
    seen = set()
    for row in rows:
        require(set(row) == {'house', 'mission', 'seed', 'helper', 'enemy', 'mode', 'mod', 'minutes'}, 'Case requires exact campaign identity fields')
        require(row['house'] in HOUSES and type(row['mission']) is int and 1 <= row['mission'] <= 22, 'Invalid authored mission')
        require(type(row['seed']) is int and 0 <= row['seed'] <= 0xffffffff, 'Invalid seed')
        require(row['helper'] in TIERS and row['enemy'] in TIERS, 'Explicit helper/enemy difficulties required')
        require(row['mode'] in ('full', 'single') and row['mod'] in ('vanilla', 'dunecity'), 'Explicit mode/content required')
        require(type(row['minutes']) is int and 1 <= row['minutes'] <= 180, 'Invalid game duration')
        key = tuple(row[k] for k in ('house', 'mission', 'seed', 'helper', 'enemy', 'mode', 'mod'))
        require(key not in seen, 'Duplicate campaign case')
        seen.add(key)
    return rows


def schedule(path, make_game):
    return [make_game(dict(name=f'partner-{r["helper"]}-v-quantbot-{r["enemy"]}', mod=r['mod'],
                          partner=r['helper'], enemy_ai='quantbot', enemy_difficulty=r['enemy'],
                          minutes=r['minutes'], measured=True), r['house'], level(r['mission']),
                      r['mission'], r['seed'], r['mode']) for r in cases(path)]


def game_id(r):
    return f'campaign/partner-{r["helper"]}-v-quantbot-{r["enemy"]}/{r["mod"]}/{r["house"]}-m{r["mission"]}/{r["seed"]}#' + ('full-campaign' if r['mode'] == 'full' else 'single-mission')


def validate_manifest(m, base):
    from research_report import compact_json, resolve, text, preregistered_cluster_sd
    require(m.get('schema') == 'dunecity-experiment/1' and m.get('population') == 'campaign', 'Invalid campaign experiment')
    require(re.fullmatch(r'[a-z0-9][a-z0-9-]{2,95}', m.get('id', '')), 'Invalid experiment id')
    require('diagnostic_house' not in m and 'diagnostic_case_index' not in m, 'Campaign cases retain their authored player house')
    require(m.get('purpose', 'strength') in ('strength', 'diagnostic'), 'Invalid purpose')
    for key in ('mechanism', 'falsifier', 'scope', 'off_contract', 'stopping_rule'):
        text(m.get(key), key)
    revision = m.get('snapshot', {}).get('revision', '')
    require(re.fullmatch('[0-9a-f]{40}', revision), 'Full source SHA required')
    text(m['snapshot'].get('cwd'), 'snapshot.cwd')
    baseline = m.get('baseline', {})
    for key in ('release', 'source_commit', 'config_identity', 'population'):
        text(baseline.get(key), 'baseline.' + key)
    require(re.fullmatch('[0-9a-f]{40}', baseline['source_commit']), 'Invalid baseline source')
    require(m.get('purpose') == 'diagnostic' or baseline.get('independent') is True, 'Independent loss baseline required')
    for key in ('observed_failure', 'successful_counterexamples', 'negative_learning'):
        require(isinstance(m.get(key), list), 'Missing evidence list ' + key)
    require(m['observed_failure'] and m['successful_counterexamples'], 'Losses and counterexamples required')
    require(set(m.get('configs', {})) == {'off', 'on'}, 'Explicit helper OFF/ON required')
    for value in m['configs'].values():
        require(re.fullmatch('[a-z0-9][a-z0-9-]+', value), 'Invalid helper style')
        require((Path(m['snapshot']['cwd']) / 'tests/ai/play-styles' / (value + '.ini')).is_file(), 'Missing helper style')
    require(m['configs']['off'] != m['configs']['on'], 'Identical treatment names')
    require(m.get('primary') == {'metric': 'won', 'direction': 'higher', 'minimum_effect': m.get('primary', {}).get('minimum_effect')}, 'Campaign primary must be player-team wins')
    require(0 < m['primary']['minimum_effect'] <= 1, 'Invalid worthwhile effect')
    design = m.get('design', {})
    require(design.get('cluster_fields') == ['seed'] and design.get('stopping') == 'fixed-budget', 'Campaign uses seed blocks and fixed budgets')
    require(0 < design.get('alpha', 0) < .5 and .5 < design.get('power', 0) < 1 and type(design.get('finalists')) is int and design['finalists'] > 0, 'Invalid error/power budget')
    preregistered_cluster_sd(design)
    stages = m.get('stages', {})
    diagnostic = m.get('purpose') == 'diagnostic'
    require(set(stages) == {'pilot'} if diagnostic else set(stages) >= {'pilot', 'confirmation'}, 'Independent confirmation required; diagnostic is pilot only')
    decks, tuning, total = {}, set(), 0
    for name, spec in stages.items():
        require(name in ('pilot', 'screen', 'evaluation', 'confirmation'), 'Invalid stage')
        require(spec.get('args', []) == [], 'Campaign options belong in frozen cases; no alternate command flags')
        path = resolve(base, spec['case_manifest'])
        rows = cases(path)
        seeds = sorted({r['seed'] for r in rows})
        if name == 'confirmation':
            from datetime import datetime
            require(datetime.fromisoformat(spec.get('locked_at', '').replace('Z', '+00:00')).utcoffset() is not None and spec.get('untouched') is True, 'Locked untouched confirmation required')
        else:
            tuning.update(seeds)
        decks[name] = dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), seeds=seeds, case_count=len(rows))
        total += 2 * len(rows)
    require(diagnostic or not tuning.intersection(decks['confirmation']['seeds']), 'Confirmation reuses tuning seeds')
    require(total <= m.get('budget', {}).get('max_games', 0) and m['budget'].get('max_wall_s', 0) >= total, 'Deck exceeds budget')
    require(set(m.get('promotion_controls', [])) == {'campaign-all-maps', 'scenario-controls', 'historical'}, 'All campaign, other-map and historical controls required')
    plans = m.get('control_plans', {})
    require(set(plans) == set(m['promotion_controls']), 'Frozen control schedules required')
    control_plans = {}
    for name, filename in plans.items():
        path = resolve(base, filename)
        control = compact_json(path)
        require(isinstance(control, list) and control and len({r['id'] for r in control}) == len(control), 'Invalid control schedule')
        if name == 'campaign-all-maps':
            require({(r.get('house'), r.get('mission')) for r in control} >= {(h, n) for h in HOUSES for n in range(1, 23)}, 'Campaign control must include all 66 authored maps')
        elif name == 'scenario-controls':
            require({r.get('category') for r in control} >= {'controls', 'skirmish', 'team', 'ladder', 'aaa'}, 'Other-map controls must cover authored maps, skirmish, team, ladder and AAA')
        control_plans[name] = dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return dict(schema='dunecity-experiment-provenance/1', population='campaign', manifest_sha256=digest(m), source_revision=revision,
                configs=m['configs'], decks=decks, control_plans=control_plans, games_per_case=2, config_values_are_run_inputs=True)


def table(directory, name):
    path = Path(directory) / name
    if not path.exists():
        path = Path(str(path) + '.gz')
    if not path.exists():
        return []
    with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as handle:
        return list(csv.DictReader(handle))


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def game_evidence(directory):
    """One retained record per game, including errors and absent telemetry."""
    games = table(directory, 'games.csv')
    require(len({r['id'] for r in games}) == len(games), 'Duplicate result game IDs')
    samples, events = collections.defaultdict(list), collections.defaultdict(list)
    for row in table(directory, 'samples.csv'):
        samples[row['id']].append(row)
    for row in table(directory, 'campaign-ai.csv'):
        events[row['id']].append(row)
    result = {}
    for row in games:
        gid = row['id']
        match = re.search(r'/(atreides|harkonnen|ordos)-m(\d+)/', gid)
        house = HOUSES[match[1]] if match else ALL_HOUSES.get(row.get('sides', '').split(':')[0], '')
        require(house != '', 'Missing tracked house identity')
        own = sorted([s for s in samples[gid] if s.get('house') == house], key=lambda s: number(s.get('minute')) or 0)
        metrics = {}
        for field in ('credits', 'army_value', 'harvesters', 'repair_yards', 'spice_gross', 'loss_value', 'kill_value', 'refineries', 'yard_level', 'heavy', 'ix', 'power', 'need', 'spent_total', 'harvesters_lost', 'carryalls'):
            values = [number(s.get(field)) for s in own]
            metrics[field] = dict(final=values[-1] if values else None, peak=max((v for v in values if v is not None), default=None),
                                  first_sampled_positive_minute=next((number(row.get('minute')) for row, value in zip(own, values) if value is not None and value > 0), None))
        parsed, malformed = [], 0
        for event in events[gid]:
            try:
                head, sep, body = event['line'].partition(' {')
                fields = dict(p.split('=', 1) for p in head.split())
                parsed.append(dict(minute=float(fields['minute']), house=fields['house'], role=fields['role'], event=fields['event'], data=json.loads('{' + body) if sep else {}))
            except (ValueError, KeyError):
                malformed += 1
        counts = collections.Counter(e['event'] for e in parsed if e['house'] == house)
        issues = []
        if row.get('error') or row.get('outcome') not in ('won', 'lost', 'time_limit'):
            issues.append('engine-error-or-unfinished')
        if not own: issues.append('missing-player-samples')
        elif number(row.get('minutes')) is None or (number(own[-1].get('minute')) or 0) + 2 < float(row['minutes']):
            issues.append('truncated-player-samples')
        if row.get('category') == 'campaign' and not parsed: issues.append('missing-campaign-events')
        if malformed: issues.append('malformed-events')
        minutes = [number(s.get('minute')) for s in own]
        if any(v is None for v in minutes) or len(set(minutes)) != len(minutes): issues.append('invalid-or-duplicate-samples')
        if len(minutes) > 1 and all(v is not None for v in minutes) and any(b - a > 2 for a, b in zip(minutes, minutes[1:])): issues.append('sample-gaps')
        result[gid] = dict(id=gid, identity={k: row.get(k, '') for k in IDENTITY}, source=row.get('source_commit'),
            source_fingerprint=row.get('source_fingerprint'), digest=row.get('digest'), outcome=row.get('outcome'),
            minutes=number(row.get('minutes')), helper_house=house, helper_style=row.get('helper_style'),
            configs=json.loads(row.get('ai_configs') or '{}'), setup=json.loads(row.get('setup_identity') or '{}'),
            metrics=metrics, event_counts=dict(counts), timeline=parsed, samples=own,
            issues=issues, interpretation='Observed events only; causal explanation requires paired evidence and review.')
    return result


def pair_records(left, right, historical=False):
    records = []
    for gid in sorted(set(left) | set(right)):
        old, new = left.get(gid), right.get(gid)
        issues = []
        if old is None or new is None:
            issues.append('unmatched-game')
        else:
            issues.extend('off:' + s for s in old['issues'])
            issues.extend('on:' + s for s in new['issues'])
            if old['identity'] != new['identity']: issues.append('scenario-identity-mismatch')
            if any(not old['setup'].get(k) or old['setup'].get(k) != new['setup'].get(k) for k in SETUP): issues.append('missing-or-changed-world-identity')
            if not historical and (not old['source'] or old['source'] != new['source'] or not old['source_fingerprint'] or old['source_fingerprint'] != new['source_fingerprint']): issues.append('source-mismatch')
            if not historical:
                h = old['helper_house']
                enemies = {k: v for k, v in old['configs'].items() if k != h}
                if not enemies or enemies != {k: v for k, v in new['configs'].items() if k != h}: issues.append('enemy-policy-unverified-or-changed')
        change = None if old is None or new is None else old['outcome'] + '->' + new['outcome']
        # Every game needs review; changed outcomes and economic/army regressions are prominent.
        record = dict(id=gid, before=old, after=new, outcome_change=change, issues=issues)
        record['flags'] = []
        if old and new:
            if old['outcome'] == 'won' and new['outcome'] != 'won': record['flags'].append('lost-baseline-win')
            if old['outcome'] == new['outcome'] == 'won' and old['minutes'] and new['minutes'] > old['minutes'] * 1.2: record['flags'].append('win-over-20-percent-slower')
            record['metric_changes'] = {k: {v: new['metrics'][k][v] - old['metrics'][k][v]
                if new['metrics'][k][v] is not None and old['metrics'][k][v] is not None else None for v in ('final', 'peak')}
                for k in old['metrics'] if k in new['metrics']}
        record['review_key'] = digest(record)
        records.append(record)
    return records


def experiment_report(path, stage, control_directory, candidate_directory, evidence=None):
    from research_report import compact_json, clustered_effect, resolve
    path = Path(path); m = compact_json(path); evidence = evidence or {}
    provenance = validate_manifest(m, path.parent)
    expected = {game_id(r): r for r in cases(provenance['decks'][stage]['path'])}
    left, right = game_evidence(control_directory), game_evidence(candidate_directory)
    rows = pair_records(left, right)
    for gid in sorted(set(expected) - (set(left) | set(right))):
        row = dict(id=gid, before=None, after=None, outcome_change=None, issues=['missing-both-arms'])
        row['review_key'] = digest(row)
        rows.append(row)
    complete = set(left) == set(right) == set(expected) and all(not r['issues'] for r in rows)
    for arm, games in (('off', left), ('on', right)):
        for gid, game in games.items():
            if gid not in expected or game['source'] != provenance['source_revision'] or game['helper_style'] != m['configs'][arm]:
                complete = False
            if gid in expected:
                case = expected[gid]
                wanted = dict(seed=str(case['seed']), mod=case['mod'], map=f"{case['house']} level {level(case['mission'])} mission {case['mission']}",
                              limit=str(case['minutes']), campaign_mode=case['mode'], partner=case['helper'], enemy_ai='quantbot', enemy_difficulty=case['enemy'])
                if any(game['identity'].get(k) != v for k, v in wanted.items()): complete = False
            # Require actual helper and all enemy configuration hashes, not requested style labels.
            if game['helper_house'] not in game['configs'] or not game['setup'].get('config_hash'):
                complete = False
    actuation = bool(rows) and all(r['before'] and r['after'] and r['before']['configs'].get(r['before']['helper_house']) and r['before']['configs'].get(r['before']['helper_house']) != r['after']['configs'].get(r['after']['helper_house']) for r in rows)
    values = [((int(r['before']['identity']['seed']),), int(r['after']['outcome'] == 'won') - int(r['before']['outcome'] == 'won'))
              for r in rows if not r['issues']]
    effect = clustered_effect(values, m['design'], m['primary']['minimum_effect'], len(provenance['decks'][stage]['seeds']), m.get('purpose') == 'diagnostic')
    pending, reviews = [], evidence.get('games', {})
    for row in rows:
        review = reviews.get(row['id'], {})
        refs = review.get('references', [])
        reviewed = (review.get('review_key') == row['review_key'] and review.get('disposition') in ('explained', 'regression-accepted', 'needs-work')
                    and bool(review.get('reason')) and bool(refs) and all(resolve(path.parent, ref).is_file() for ref in refs))
        row['review'] = review if reviewed else None
        if not reviewed or review.get('disposition') == 'needs-work': pending.append(row['id'])
    controls = {}
    for name in m['promotion_controls']:
        ref = evidence.get('controls', {}).get(name)
        controls[name] = False
        if ref:
            c = compact_json(resolve(path.parent, ref))
            plan = provenance['control_plans'][name]
            controls[name] = bool(c.get('schema') == 'campaign-research-control/1' and c.get('kind') == name
                and c.get('source_revision') == provenance['source_revision'] and c.get('candidate_config') == m['configs']['on']
                and c.get('schedule_sha256') == plan['sha256'] and c.get('complete') is True
                and c.get('pending_reviews') == [] and c.get('games')
                and c.get('games_sha256') == digest(c['games']))
    def attested(key):
        r = evidence.get(key, {})
        return r.get('passed') is True and r.get('references') and all(resolve(path.parent, x).is_file() for x in r['references'])
    confirmed = bool(stage == 'confirmation' and m.get('purpose') != 'diagnostic' and complete and actuation and not pending
                     and effect['power_sufficient'] and effect['interval'] and effect['interval'][0] > 0
                     and effect['effect'] >= m['primary']['minimum_effect'] and attested('mechanism') and attested('off_contract') and attested('independent_holdout'))
    strata = collections.defaultdict(lambda: collections.Counter())
    for row in rows:
        if row['before'] and row['after']:
            key = tuple(row['before']['identity'][k] for k in ('map', 'campaign_mode', 'mod', 'partner', 'enemy_difficulty'))
            strata[key][row['outcome_change']] += 1
    return dict(schema='dunecity-experiment-report/1', id=m['id'], stage=stage, population='campaign', provenance=provenance,
                status='confirmed' if confirmed else 'inconclusive',
                facts=dict(complete=complete, actuation_verified=actuation, quality_reviewed=not pending and complete,
                           promotion_controls_passed=all(controls.values()), promotion_ready=confirmed and all(controls.values())),
                metrics=dict(primary=effect), games=rows, pending_reviews=pending, controls=controls,
                strata=[dict(identity=list(k), outcomes=dict(v)) for k, v in sorted(strata.items())],
                interpretation='Player-support OFF/ON against fixed enemies. Every game is retained; no automatic causal or default-promotion claim.')


def control_report(before, after, schedule_path, kind, candidate_config, reviews=None):
    from research_report import compact_json
    schedule_path = Path(schedule_path)
    expected = compact_json(schedule_path)
    require(isinstance(expected, list) and expected, 'Explicit control schedule required')
    require(kind in ('campaign-all-maps', 'scenario-controls', 'historical'), 'Unknown control kind')
    left, right = game_evidence(before), game_evidence(after)
    rows = pair_records(left, right, historical=kind == 'historical')
    ids = {r['id'] for r in expected}
    complete = len(ids) == len(expected) and set(left) == set(right) == ids and bool(rows) and all(not r['issues'] for r in rows)
    for expected_row in expected:
        for data in (left, right):
            actual = data.get(expected_row['id'])
            if actual is None: continue
            for key in ('seed', 'mod', 'map'):
                if key not in expected_row or str(expected_row[key]) != actual['identity'][key]: complete = False
            if str(expected_row.get('minutes', '')) != actual['identity']['limit']: complete = False
            if 'sides' in expected_row:
                sides = expected_row['sides']
                wanted = sides if isinstance(sides, str) else ' '.join(':'.join(map(str, side)) for side in sides)
                if wanted != actual['identity']['sides']: complete = False
            else: complete = False
    sources = {r['source'] for r in right.values()}
    complete &= len(sources) == 1 and all(r['helper_style'] == candidate_config or r['setup'].get('style') == candidate_config for r in right.values())
    if kind == 'historical':
        complete &= len({r['source'] for r in left.values()}) == 1 and {r['source'] for r in left.values()} != sources
    pending = []
    reviews = reviews or {}
    for row in rows:
        review = reviews.get(row['id'], {})
        valid = (review.get('review_key') == row['review_key'] and review.get('disposition') in ('explained', 'regression-accepted')
                 and bool(review.get('reason')) and bool(review.get('references')) and all(Path(ref).is_file() for ref in review['references']))
        row['review'] = review if valid else None
        if not valid: pending.append(row['id'])
    return dict(schema='campaign-research-control/1', kind=kind, source_revision=next(iter(sources)) if len(sources) == 1 else None,
                candidate_config=candidate_config, schedule_sha256=hashlib.sha256(schedule_path.read_bytes()).hexdigest(),
                complete=bool(complete), games=rows, games_sha256=digest(rows), pending_reviews=pending,
                interpretation='Historical whole-version same-scenario comparison, not head-to-head strength.' if kind == 'historical' else 'Matched controls; each regression requires explicit review.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--historical', action='store_true')
    parser.add_argument('--control-schedule', type=Path)
    parser.add_argument('--kind', choices=('campaign-all-maps', 'scenario-controls', 'historical'))
    parser.add_argument('--candidate-config')
    parser.add_argument('--reviews', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.control_schedule:
        require(args.kind and args.candidate_config, 'Control kind and config required')
        report = control_report(args.before, args.after, args.control_schedule, args.kind, args.candidate_config, json.loads(args.reviews.read_text()) if args.reviews else None)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        return
    rows = pair_records(game_evidence(args.before), game_evidence(args.after), args.historical)
    args.output.write_text(json.dumps(dict(schema='campaign-game-comparison/1', games=rows,
        interpretation='Historical whole-version same-scenario comparison; not head-to-head or isolated AI causality.' if args.historical else 'Descriptive paired observations; no promotion claim.',
        complete=bool(rows) and all(not r['issues'] for r in rows)), indent=2) + '\n')


if __name__ == '__main__':
    main()
