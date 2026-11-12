"""Synthetic receipt counterexamples; none of these fixtures is native AI evidence."""
import argparse
import base64
import copy
import csv
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import genome_model
import genome_qualification as gate
import genome_search as search


class CandidateQualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.registry = genome_model.load_registry()
        cls.contracts = gate.contracts(source_fingerprint='3' * 64)
        cls.item = dict(id='growth-and-defence',
                        values=dict(city_growth_power=0, threat_turrets=1), expression=[])
        cls.source = '1' * 40
        cls.identity = genome_model.genome_id(cls.item)

    def proof(self, name, seed):
        contract = self.contracts[name]
        phases = gate.phase_payloads(self.item, contract, (0, 10, 20), self.registry)
        requested = gate.requested_ini(self.item, contract, self.registry)
        effective = gate.options(requested)
        configured = [dict(house='1', genome=self.item['id'], phase=phase, start_cycle=str(minute * gate.CPM),
                           config_hash=('%x' % (index + 10)) * 16, difficulty='3', controller='0')
                      for index, (phase, minute) in enumerate(zip(genome_model.PHASES, (0, 10, 20)))]
        applied = [dict(house='1', genome=self.item['id'], phase=row['phase'], cycle=row['start_cycle'],
                        config_hash=row['config_hash'], difficulty='3', controller='0') for row in configured]
        return dict(suite=name, source_commit=self.source, genome=copy.deepcopy(self.item),
                    genome_digest=self.identity, seed=seed, phase_minutes=[0, 10, 20],
                    requested_map_sha256=contract['map_sha256'], effective_map_sha256=contract['map_sha256'],
                    frozen_options_sha256=contract['options_sha256'], requested_options_ini=requested,
                    requested_options_sha256=gate.sha(requested.encode()), effective_options=effective,
                    effective_options_sha256=gate.sha(gate.canonical(effective).encode()),
                    phase_inputs=phases, overlay_sha256=gate.overlay_hash(phases), probe_sha256='2' * 64,
                    source_fingerprint='3' * 64, source_fingerprint_error='',
                    source_fingerprint_version=contract['source_fingerprint_version'], working_tree_modified=False, error='',
                    setup=dict(seed=str(seed), cycle='0', observing='1', city_sim='0', style='default',
                               options_hash=gate.native_options_hash(effective, contract['rule_schema'])),
                    result=dict(mode='match', cycle=str(35 * gate.CPM), winner_team='2', outcome='decided'),
                    roster=[dict(house=str(gate.NUMBERS[house]), team=str(team),
                                 players='qBotBrutal' if code == 'QB' else 'AIPlayerHard')
                            for house, team, code in gate.ROSTER],
                    configured=configured, applied=applied, candidate_config_hash=configured[0]['config_hash'],
                    houses=[dict(house=str(number), alive='1' if number == 1 else '0') for number in range(5)])

    def all_proofs(self):
        return [self.proof(name, seed) for name, contract in self.contracts.items() for seed in contract['seeds']]

    def write_receipts(self, directory, proofs):
        rows = []
        for index, proof in enumerate(proofs):
            name = 'proof-%d.json.gz' % index
            path = directory / name
            with gzip.open(path, 'wt', encoding='utf-8') as stream:
                json.dump(proof, stream)
            rows.append(dict(source_commit=proof['source_commit'], seed=proof['seed'],
                             qualification_suite=proof['suite'], candidate_genome=self.item['id'], genome_digest=self.identity,
                             genome_overlay_sha256=proof['overlay_sha256'], effective_options_sha256=proof['effective_options_sha256'],
                             qualification_evidence=name, qualification_evidence_sha256=gate.sha(path.read_bytes()),
                             qualification_status='feasible'))
        self.write_rows(directory, rows)
        return rows

    @staticmethod
    def write_rows(directory, rows):
        with (directory / 'games.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else ['genome_digest'])
            writer.writeheader()
            writer.writerows(rows)

    def verdict(self, proofs, edit_rows=None):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            rows = self.write_receipts(directory, proofs)
            if edit_rows:
                edit_rows(rows)
                self.write_rows(directory, rows)
            with patch.object(gate, 'pinned_contracts', return_value=(self.contracts, self.registry)):
                return gate.verdicts([directory], self.source, [self.item])[self.identity]

    def test_exact_six_plus_seven_native_wins_are_required(self):
        self.assertEqual([len(value['seeds']) for value in self.contracts.values()], [6, 7])
        result = self.verdict(self.all_proofs())
        self.assertEqual(result['status'], 'feasible')
        self.assertEqual(set(result['suites'].values()), {'feasible'})

    def test_default_off_ctest_and_missing_seed_cannot_qualify_candidate(self):
        self.assertEqual(gate.verdicts([], self.source, [self.item])[self.identity]['status'], 'unverified')
        self.assertEqual(self.verdict(self.all_proofs()[:-1])['status'], 'unverified')
        self.assertEqual(self.verdict(self.all_proofs()[:6])['status'], 'unverified')
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.write_rows(directory, [dict(outcome='won', winner_team='2', test='quantbot_all_against_atreides')])
            with patch.object(gate, 'pinned_contracts', return_value=(self.contracts, self.registry)):
                result = gate.verdicts([directory], self.source, [self.item])
            self.assertEqual(result[self.identity]['status'], 'unverified')

    def test_stale_source_wrong_genome_and_missing_provenance_are_unverified(self):
        mutations = (
            lambda p: p.update(source_commit='5' * 40),
            lambda p: p['genome']['values'].update(threat_turrets=0),
            lambda p: p.update(genome_digest='6' * 64),
            lambda p: p.update(effective_options_sha256='7' * 64),
            lambda p: p.update(requested_options_ini='[Game Options]\nInstant Build=true\n'),
            lambda p: p.update(working_tree_modified=True),
            lambda p: p['roster'][1].update(players='qBotHard'),
            lambda p: p['roster'][2].update(team='3'),
            lambda p: p['configured'][0].update(difficulty='2'),
            lambda p: p['applied'][0].update(difficulty='2'),
            lambda p: p['setup'].update(city_sim='1'),
            lambda p: p.update(applied=[]),
            lambda p: p.pop('probe_sha256'),
            lambda p: p.pop('effective_options'),
            lambda p: p['setup'].update(seed='999'),
            lambda p: p.update(working_tree_modified=None),
            lambda p: p.update(working_tree_modified=0),
            lambda p: p.pop('working_tree_modified'),
            lambda p: p.update(source_fingerprint='5' * 64),
            lambda p: p.pop('source_fingerprint'),
            lambda p: p.update(source_fingerprint_error='cannot certify source'),
            lambda p: p.pop('source_fingerprint_error'),
            lambda p: p.pop('source_fingerprint_version'),
            lambda p: p['setup'].update(options_hash='4' * 16),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(counterexample=index):
                proofs = self.all_proofs()
                mutate(proofs[0])
                self.assertEqual(self.verdict(proofs)['status'], 'unverified')

    def test_loss_draw_timeout_and_one_cycle_late_reject(self):
        for outcome, winner, cycle in [('won', '1', 35 * gate.CPM), ('time_limit', '0', 41 * gate.CPM),
                                        ('lost', '2', 40 * gate.CPM + 1)]:
            with self.subTest(outcome=outcome, winner=winner, cycle=cycle):
                proofs = self.all_proofs()
                proofs[0]['result'].update(outcome=outcome, winner_team=winner, cycle=str(cycle))
                self.assertEqual(self.verdict(proofs)['status'], 'infeasible')

    def test_exact_deadline_is_allowed_not_rounded_late_win(self):
        proofs = self.all_proofs()
        proofs[0]['result']['cycle'] = str(40 * gate.CPM)
        self.assertEqual(self.verdict(proofs)['status'], 'feasible')

    def test_future_unreached_phase_is_not_required_but_reached_phase_is(self):
        proofs = self.all_proofs()
        proofs[0]['result']['cycle'] = str(5 * gate.CPM)
        proofs[0]['applied'] = proofs[0]['applied'][:1]
        self.assertEqual(self.verdict(proofs)['status'], 'feasible')
        proofs[0]['result']['cycle'] = str(11 * gate.CPM)
        self.assertEqual(self.verdict(proofs)['status'], 'unverified')

    def test_retry_cannot_erase_failure_or_unverified_receipt(self):
        proofs = self.all_proofs()
        failed = copy.deepcopy(proofs[0])
        failed['result'].update(outcome='time_limit', winner_team='0', cycle=str(41 * gate.CPM))
        self.assertEqual(self.verdict([failed] + proofs)['status'], 'infeasible')
        def corrupt_retry(rows):
            rows[-1]['qualification_evidence_sha256'] = '0' * 64
        self.assertEqual(self.verdict(proofs + [copy.deepcopy(proofs[0])], corrupt_retry)['status'], 'unverified')

    def test_only_actual_atreides_receives_context_resolved_phase_overlays(self):
        encoded = base64.b64encode(json.dumps(dict(schema_version=1, genomes=[self.item])).encode()).decode()
        args = argparse.Namespace(manifest_b64=encoded, genome=self.item['id'], phase_minutes=[0, 10, 20],
                                  qualification_source=self.source)
        for name, contract in self.contracts.items():
            with self.subTest(suite=name), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp) / 'inputs'
                with patch.object(gate, 'pinned_contracts', return_value=(self.contracts, self.registry)):
                    gate.prepare(args, dict(cell=contract['cell']), directory)
                actual = {p.name: p.read_text() for p in directory.glob('*.ini') if p.name == 'schedule.ini' or p.name.startswith('1-')}
                self.assertEqual(actual, gate.phase_payloads(self.item, contract, args.phase_minutes, self.registry))
                self.assertNotIn('[Map]', actual['schedule.ini'])
                self.assertEqual(actual['schedule.ini'].split('[Genomes]\n')[1].strip(), '1=' + self.item['id'])
                self.assertIn('threatturrets=1', actual['1-opening.ini'].lower())
                self.assertEqual(gate.options((directory / 'game-options.ini').read_text())['technology installments'], '0')

    def test_registered_global_rule_is_applied_not_silently_dropped(self):
        candidate = copy.deepcopy(self.item)
        candidate['values']['technology_installments'] = 1
        requested = gate.requested_ini(candidate, self.contracts[gate.NAMES[0]], self.registry)
        self.assertEqual(gate.options(requested)['technology installments'], '1')
        self.assertEqual(gate.options(requested)['instant build'], '0')
        proof = self.proof(gate.NAMES[0], self.contracts[gate.NAMES[0]]['seeds'][0])
        proof['genome'], proof['genome_digest'] = candidate, genome_model.genome_id(candidate)
        proof['requested_options_ini'], proof['requested_options_sha256'] = requested, gate.sha(requested.encode())
        proof['effective_options'] = gate.options(requested)
        proof['effective_options_sha256'] = gate.sha(gate.canonical(proof['effective_options']).encode())
        proof['setup']['options_hash'] = gate.native_options_hash(proof['effective_options'], self.contracts[gate.NAMES[0]]['rule_schema'])
        self.assertEqual(gate.native_verdict(proof, candidate, self.source, self.contracts[gate.NAMES[0]],
                                             self.registry, (0, 10, 20)), 'feasible')

    def test_production_feasibility_always_combines_native_and_issue_checks(self):
        case = dict(id='synthetic', adapted=True, game_rules=dict(technology_installments=0))
        row = dict(case='synthetic', genome_digest=self.identity, source_commit=self.source,
                   technology_installments='off', completed='1', telemetry='complete', error='', feasibility='feasible',
                   checks_sha256=gate.sha(search.BEHAVIOUR_CHECKS.read_bytes()), registry_sha256=search.digest(self.registry))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'checks.csv'
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(row)); writer.writeheader(); writer.writerow(row)
            with patch.object(search, 'load_json', return_value=dict(cases=[case])):
                actual = search.feasibility_table([path], source=self.source, genomes=[self.item], registry=self.registry)
                self.assertEqual(actual[self.identity], 'unverified')
                for state in ('feasible', 'infeasible', 'unverified'):
                    with patch.object(gate, 'verdicts', return_value={self.identity: dict(status=state)}):
                        actual = search.feasibility_table([path], source=self.source, genomes=[self.item], registry=self.registry)
                        self.assertEqual(actual[self.identity], state)

    def test_saved_variants_match_native_enum_not_sorted_vocabulary(self):
        native = ('easy', 'medium', 'hard', 'brutal', 'defend')
        self.assertEqual(gate.transport().DIFFICULTIES, native)
        item = dict(id='by-native-tier', values=dict(focus_fire=0),
                    expression=[dict(when=dict(difficulty=['brutal']), values=dict(focus_fire=1))])
        context = gate.context(self.contracts[gate.NAMES[0]])
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / 'phases'
            gate.transport().render_phase_inputs([(1, item['id'])], context, {item['id']: item}, directory,
                                                registry=self.registry, difficulty_variants=True)
            for number, difficulty in enumerate(native):
                for phase in genome_model.PHASES:
                    expected = genome_model.render_ini(item, dict(context, difficulty=difficulty, phase=phase), self.registry)
                    self.assertEqual((directory / ('1-%d-%s.ini' % (number, phase))).read_text(), expected)

    def test_native_result_shape_uses_existing_setup_seed_only(self):
        proof = self.proof(gate.NAMES[0], self.contracts[gate.NAMES[0]]['seeds'][0])
        proof['result'] = gate._markers([
            'SCENARIO_RESULT: mode=match cycle=131250 end_s=2100.0 minutes=35.00 outcome=decided '
            'winner_team=2 triggers=0 fired=0 win_flags=0 quota=0 city_pop=0'], 'SCENARIO_RESULT')[0]
        self.assertNotIn('seed', proof['result'])
        self.assertEqual(gate.native_verdict(proof, self.item, self.source, self.contracts[gate.NAMES[0]],
                                             self.registry, (0, 10, 20)), 'feasible')

    def test_complete_effective_rules_cannot_omit_or_change_unlisted_fixture_options(self):
        for key in ('research lab', 'plan building sites', 'chaos mode'):
            for remove in (True, False):
                with self.subTest(option=key, missing=remove):
                    proofs = self.all_proofs()
                    proof = proofs[0]
                    if remove:
                        del proof['effective_options'][key]
                    else:
                        proof['effective_options'][key] = '0' if proof['effective_options'][key] == '1' else '1'
                        proof['setup']['options_hash'] = gate.native_options_hash(
                            proof['effective_options'], self.contracts[gate.NAMES[0]]['rule_schema'])
                    proof['effective_options_sha256'] = gate.sha(gate.canonical(proof['effective_options']).encode())
                    self.assertEqual(self.verdict(proofs)['status'], 'unverified')

    def test_capture_preserves_source_fingerprint_error(self):
        proof = self.proof(gate.NAMES[0], self.contracts[gate.NAMES[0]]['seeds'][0])
        encoded = base64.b64encode(json.dumps(dict(schema_version=1, genomes=[self.item])).encode()).decode()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            directory = root / 'inputs'
            args = argparse.Namespace(manifest_b64=encoded, genome=self.item['id'], phase_minutes=[0, 10, 20],
                                      qualification_source=self.source, results=str(root))
            game = dict(cell=self.contracts[gate.NAMES[0]]['cell'], seed=proof['seed'], id='synthetic-native-shape')
            with patch.object(gate, 'pinned_contracts', return_value=(self.contracts, self.registry)):
                gate.prepare(args, game, directory)
                (directory / 'effective-options.ini').write_text(proof['requested_options_ini'])
                (directory / 'map.ini').write_bytes((gate.ROOT / 'data/maps/singleplayer/5P - 128x128 - All against Atreides.ini').read_bytes())
                binary = root / 'synthetic-probe'
                binary.write_bytes(b'synthetic probe identity, not native evidence')
                def marker(name, values):
                    return name + ': ' + ' '.join(key + '=' + str(value) for key, value in values.items())
                lines = [marker('SCENARIO_SETUP', proof['setup'])]
                lines += [marker('SCENARIO_ROSTER', row) for row in proof['roster']]
                lines += [marker('GENOME_CONFIG', row) for row in proof['configured']]
                lines += [marker('GENOME_PHASE', row) for row in proof['applied']]
                lines += [marker('SCENARIO_AI_CONFIG', dict(house=1, config_hash=proof['candidate_config_hash']))]
                row = dict(source_commit=self.source, source_fingerprint=proof['source_fingerprint'], error='')
                summary = dict(workingTreeModified=False, sourceFingerprintError='cannot fingerprint source input')
                result = gate.capture(args, game, directory, lines, proof['result'], proof['houses'], binary, row, summary)
            stored = json.loads(gzip.decompress((root / result['qualification_evidence']).read_bytes()))
            self.assertEqual(stored['source_fingerprint_error'], 'cannot fingerprint source input')
            self.assertEqual(result['qualification_status'], 'unverified')

    def test_native_smoke_cannot_claim_complete_qualification(self):
        encoded = base64.b64encode(json.dumps(dict(schema_version=1, genomes=[self.item])).encode()).decode()
        with tempfile.TemporaryDirectory() as temp:
            args = argparse.Namespace(manifest_b64=encoded, genome=self.item['id'], phase_minutes=[0, 10, 20],
                                      qualification_source=self.source, results=temp, qualification_smoke=True)
            with patch.object(gate, 'verdicts', return_value={self.identity: dict(status='feasible')}):
                self.assertFalse(gate.check_run(args))


class SourceIdentityTests(unittest.TestCase):
    def test_source_identity_matches_committed_blobs_not_modified_worktree(self):
        import subprocess
        from source_provenance import source_fingerprint, pinned_source_fingerprint
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(['git', 'init', '--quiet', str(root)], check=True)
            (root / 'source.cpp').write_bytes(b'int source = 1;\n')
            (root / 'nested').mkdir()
            for index in range(24):
                (root / 'nested' / f'{index}.bin').write_bytes(bytes(range(256)) * (index + 1))
            for name in ('build-sibling', 'out-sibling'):
                (root / name).mkdir()
                (root / name / 'source.bin').write_bytes(b'included sibling\0payload')
            subprocess.run(['git', 'config', 'core.symlinks', 'false'], cwd=root, check=True)
            (root / 'logical-link').write_bytes(b'source.cpp')
            subprocess.run(['git', 'add', 'source.cpp', 'nested', 'build-sibling', 'out-sibling'], cwd=root, check=True)
            link_oid = subprocess.check_output(['git', 'hash-object', '-w', 'logical-link'], cwd=root, text=True).strip()
            subprocess.run(['git', 'update-index', '--add', '--cacheinfo',
                            '120000,' + link_oid + ',logical-link'], cwd=root, check=True)
            subprocess.run(['git', '-c', 'user.name=Source contract', '-c', 'user.email=source@example.invalid',
                            'commit', '--quiet', '-m', 'Source fixture'], cwd=root, check=True)
            source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
            certified = pinned_source_fingerprint(root, source)
            resolved = []
            resolve = Path.resolve
            def counted_resolve(path, *args, **kwargs):
                resolved.append(path)
                return resolve(path, *args, **kwargs)
            with patch.object(Path, 'resolve', counted_resolve):
                self.assertEqual(source_fingerprint(root, root / 'build', root / 'out'), certified)
            self.assertEqual(resolved.count((root / 'nested').resolve()), 1)
            # Excluded generated inputs stay excluded even when removed after
            # being listed in the index. They must not turn into source errors.
            for name in ('build', 'out'):
                directory = root / name
                directory.mkdir()
                generated = directory / 'gone.txt'
                generated.write_text('excluded')
                subprocess.run(['git', 'add', str(generated.relative_to(root))], cwd=root, check=True)
                generated.unlink()
            self.assertEqual(source_fingerprint(root, root / 'build', root / 'out'), certified)
            (root / 'source.cpp').write_bytes(b'int source = 2;\n')
            self.assertNotEqual(source_fingerprint(root, root / 'build', root / 'out'), certified)
            self.assertEqual(pinned_source_fingerprint(root, source), certified)


if __name__ == '__main__':
    unittest.main()
