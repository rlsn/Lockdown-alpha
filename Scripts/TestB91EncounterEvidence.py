import json
import tempfile
import unittest
from pathlib import Path
from CheckB91Encounter import check


class EncounterEvidenceTests(unittest.TestCase):
    def setUp(self):
        artifacts = Path(__file__).resolve().parents[1]/'Artifacts'
        artifacts.mkdir(exist_ok=True)
        self.directory = tempfile.TemporaryDirectory(prefix='EncounterEvidence-', dir=artifacts)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.report = {
            'complete': True, 'phase': 27, 'failures': 0,
            'walked_cm': 10001, 'falling_s': 0,
            'checks': [
                {'case': f'Actual remote reference phase{phase} completes', 'passed': True}
                for phase in range(1, 28)
            ] + [
                {'case': case, 'passed': True} for case in (
                    'Reference uses named finite storage, foliage, grass, roof anchor and existing tools',
                    'Riverport warehouse crate is physically indoors with its original empty storage',
                    'Labelled starting-tool fixture transfers three existing instances without minting',
                    'One RMB order opens the rear after walking from public frontage',
                    'Real storage transaction places the original Knife inside the warehouse crate',
                    'Current private container quote recovers exactly the stored Knife identity',
                    'Prepared original Rope installs at the approved Riverport roof pair',
                    'Actual optional link takes the same owner onto the supported roof landing',
                    'Deployer recovers the same portable Rope after a permanent-stair return',
                    'Harvest confirmation begins outside item reach',
                    'One item confirmation walks into reach then channels4s and yields one source-backed Fiber',
                    'Continuous frontage, rear, warehouse, roof and grass journey ends concealed from the remote observer',
                    'Reference journey records actual movement without falling or restaging',
                    'Same owner pays retained Torch fuel and permanently clears the escape grass',
                    'Paid functional grass ignition leaves all3000 scenery trees intact',
                    'Journey conserves all initial goods plus exactly one finite harvest reward',
                )
            ],
        }
        self.write_report()
        for phase in range(1, 28):
            for role in ('A', 'B'):
                self.write(f'{role}_{phase}.json', {'completed': True, 'phase': phase})

    def write(self, name, data):
        (self.root/name).write_text(json.dumps(data), encoding='utf-8')

    def write_report(self):
        self.write('Encounter.json', self.report)

    def reject_report(self):
        self.write_report()
        with self.assertRaises(AssertionError):
            check(self.root)

    def test_connected_evidence_needs_no_persistence_files(self):
        self.assertEqual(check(self.root), self.report)
        self.assertEqual(len(list(self.root.iterdir())), 55)

    def test_old_post_retirement_phase_is_rejected(self):
        self.report['phase'] = 91
        self.reject_report()

    def test_incomplete_server_is_rejected(self):
        self.report['complete'] = False
        self.reject_report()

    def test_failure_summary_is_rejected(self):
        self.report['failures'] = 1
        self.reject_report()

    def test_failed_interaction_is_rejected(self):
        self.report['checks'][27]['passed'] = False
        self.reject_report()

    def test_missing_interaction_is_rejected(self):
        self.report['checks'].pop(27)
        self.reject_report()

    def test_repeated_unrelated_passes_cannot_replace_authority_checks(self):
        for row in self.report['checks'][27:-1]:
            row['case'] = 'Repeated unrelated pass'
        self.reject_report()

    def test_duplicate_case_is_rejected(self):
        self.report['checks'].append(self.report['checks'][0].copy())
        self.reject_report()

    def test_unrelated_pass_cannot_replace_one_authority_check(self):
        self.report['checks'][27]['case'] = 'Unrelated authority pass'
        self.reject_report()

    def test_missing_phase_attestation_is_rejected(self):
        self.report['checks'][26]['case'] = 'Unrelated fixture'
        self.reject_report()

    def test_missing_conservation_is_rejected(self):
        self.report['checks'][-1]['case'] = 'Unrelated fixture'
        self.reject_report()

    def test_distance_threshold_is_strict(self):
        self.report['walked_cm'] = 10000
        self.reject_report()

    def test_falling_threshold_is_strict(self):
        self.report['falling_s'] = .1
        self.reject_report()

    def test_missing_client_phase_is_rejected(self):
        (self.root/'B_27.json').unlink()
        with self.assertRaises(FileNotFoundError):
            check(self.root)

    def test_incomplete_client_phase_is_rejected(self):
        self.write('A_27.json', {'completed': False, 'phase': 27})
        with self.assertRaises(AssertionError):
            check(self.root)

    def test_mismatched_client_phase_is_rejected(self):
        self.write('B_27.json', {'completed': True, 'phase': 1})
        with self.assertRaises(AssertionError):
            check(self.root)


if __name__ == '__main__':
    unittest.main()
