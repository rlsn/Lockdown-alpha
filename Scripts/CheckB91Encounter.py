"""Validate the connected encounter without a world persistence process."""
import json
import sys
from pathlib import Path


AUTHORITY_CASES = frozenset({
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
})
EXPECTED_CASES = AUTHORITY_CASES | {
    f'Actual remote reference phase{phase} completes' for phase in range(1, 28)
}


def check(root):
    report = json.loads((root/'Encounter.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['phase'] == 27 and not report['failures']
    assert all(row['passed'] for row in report['checks'])
    cases = [row['case'] for row in report['checks']]
    unique_cases = set(cases)
    assert len(cases) == len(unique_cases), 'Duplicate encounter case evidence'
    assert unique_cases == EXPECTED_CASES, 'Missing or unexpected encounter case evidence'
    assert report['walked_cm'] > 10000 and report['falling_s'] < .1
    for phase in range(1, 28):
        for role in ('A', 'B'):
            data = json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig'))
            assert data['completed'] and data['phase'] == phase, f'{role}_{phase}.json does not acknowledge phase {phase}'
    return report


if __name__ == '__main__':
    result = check(Path(sys.argv[1]))
    print('PASS B91 connected reference:', len(result['checks']), 'checks')
