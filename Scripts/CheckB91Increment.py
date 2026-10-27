"""Verify the completed B91 structural/door increment; never close later cases."""
import hashlib
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from CheckB91Evidence import check as structural
from CheckB91Network import check as network
from CheckB90Closure import check as closure
from CheckEconomyEvidence import verify as economy


def check(directory):
    read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
    structural(directory/'structural')
    for role in ('A', 'B'):
        folder = directory/'network'/role
        network(folder)
        video = read(folder/'VideoSummary.json')['clients'][role]
        assert video['frames'] >= 10 and video['duration_seconds'] > 3
        assert hashlib.sha256((folder/f'{role}.mp4').read_bytes()).hexdigest() == video['video_sha256']
    closure(directory/'regression')
    economy(directory/'regression/legacy/Ledger.json')
    rules = read(directory/'NativeRules.json')
    assert rules['succeeded'] == 51 and rules['failed'] == rules['notRun'] == rules['inProcess'] == 0
    for name in ('BuildEditor.log', 'BuildGame.log'):
        assert 'Result: Succeeded' in (directory/name).read_text(encoding='utf-8-sig')
    summary = dict(stage='B91', status='In progress', increment='Structural interiors, permanent stairs and authoritative doors',
                   completed_cases=['B91-T01', 'B91-T02', 'B91-T03'],
                   open_cases=[f'B91-T{i:02d}' for i in range(4, 16)], full_stage_acceptance=False,
                   native_walks=52, native_rules=51,
                   video_scope='Two independent owning clients per run; A/B recorded in separate runs with one renderer and one headless peer; reduced 720p settings, ~5Hz timestamped frames',
                   later_stage_dependencies=['B92 population/economy/security', 'B93 earned full-map victory', 'B94 full-session/stress/human acceptance'])
    (directory/'IncrementSummary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('PASS B91 structural/door increment; T01-T03 evidenced, T04-T15 explicitly open')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
