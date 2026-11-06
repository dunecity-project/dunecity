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
        cls.contracts = gate.contracts()
        cls.item = dict(id='conservative-nuclear-first-factory',
                        values=dict(city_compact_power=1, first_factory_own_prerequisites=1), expression=[])
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
                    source_fingerprint='3' * 64, working_tree_modified=False, error='',
                    setup=dict(seed=str(seed), cycle='0', observing='1', city_sim='0', style='default', options_hash='4' * 16),
                    result=dict(mode='match', seed=str(seed), cycle=str(35 * gate.CPM), winner_team='2', outcome='lost'),
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
            return gate.verdicts([directory], self.source, [self.item], local_contracts=(self.contracts, self.registry))[self.identity]

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
            result = gate.verdicts([directory], self.source, [self.item], local_contracts=(self.contracts, self.registry))
            self.assertEqual(result[self.identity]['status'], 'unverified')

    def test_stale_source_wrong_genome_and_missing_provenance_are_unverified(self):
        mutations = (
            lambda p: p.update(source_commit='5' * 40),
            lambda p: p['genome']['values'].update(first_factory_own_prerequisites=0),
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
            lambda p: p['result'].update(seed='999'),
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
        args = argparse.Namespace(manifest_b64=encoded, genome=self.item['id'], phase_minutes=[0, 10, 20])
        for name, contract in self.contracts.items():
            with self.subTest(suite=name), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp) / 'inputs'
                gate.prepare(args, dict(cell=contract['cell']), directory)
                actual = {p.name: p.read_text() for p in directory.glob('*.ini') if p.name == 'schedule.ini' or p.name.startswith('1-')}
                self.assertEqual(actual, gate.phase_payloads(self.item, contract, args.phase_minutes, self.registry))
                self.assertNotIn('[Map]', actual['schedule.ini'])
                self.assertEqual(actual['schedule.ini'].split('[Genomes]\n')[1].strip(), '1=' + self.item['id'])
                self.assertIn('firstfactoryownprerequisites=1', actual['1-opening.ini'].lower())
                self.assertEqual(gate.options((directory / 'game-options.ini').read_text())['technology installments'], '0')

    def test_registered_global_rule_is_applied_not_silently_dropped(self):
        candidate = copy.deepcopy(self.item)
        candidate['values']['technology_installments'] = 1
        requested = gate.requested_ini(candidate, self.contracts[gate.NAMES[0]], self.registry)
        self.assertEqual(gate.options(requested)['technology installments'], '1')
        self.assertEqual(gate.options(requested)['instant build'], '0')

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


if __name__ == '__main__':
    unittest.main()
