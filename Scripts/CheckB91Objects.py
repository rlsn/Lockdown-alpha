#!/usr/bin/env python3
"""Inspect native two-owner storage/cargo receipts; never promote the whole stage."""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path


def check(root):
    report = json.loads((root/'Objects.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and report['failures'] == 0
    assert report['phase'] == 101
    assert len(report['checks']) == (51 if report.get('furniture_scenario') else 49) and all(c['passed'] for c in report['checks'])
    size = len(report['fitted_objects'])
    assert size in (156, 204, 232, 264, 328, 339)
    finite_count = 84 if size == 156 else 143 if size == 339 else 132
    assert report['finite_initial_items'] == finite_count and report['conserved']
    initial = report['initial_item_ids']
    physical = report['physical_items']
    assert len(initial) == len(set(initial)) == len(physical) == finite_count
    assert set(initial) == {r['id'] for r in physical}
    assert all(r['origin'].startswith('prop_') and r['origin_receipt'] for r in physical)
    broken = report['break_item_ids']
    assert len(broken) == 2 and len(set(broken)) == 2
    assert all(r['state'] == 0 and r['location'] == 'ground' for r in physical if r['id'] in broken)
    manifest = report['fitted_objects']
    assert len({r['source_id'] for r in manifest}) == size
    expected = {1: 48, 2: 36, 3: 24, 4: 24, 5: 24}
    if size >= 204:
        expected.update({7: 24, 8: 12, 9: 12})
    if size >= 232:
        expected.update({10: 12, 11: 16})
    if size >= 264:
        expected[12] = 32
    if size >= 328:
        expected[13] = 64
    if size == 339:
        expected[14] = 11
    assert collections.Counter(r['kind'] for r in manifest) == expected
    for row in manifest:
        assert math.dist(row['source_cm'][:2], row['fitted_cm'][:2]) <= 1000.01
        assert row['placement_valid']
    fields = {'Handle', 'Kind', 'Position', 'Revision', 'State', 'PhysicalRevision'}
    received = []
    for path in sorted(root.glob('[AB]_*.json')):
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        if data.get('fixture') != 'objects':
            continue
        assert data['completed'], path.name
        assert set(data['wire_fields']) in (fields, fields | {'Endpoint'})
        assert data['duplicate_same']
        assert all(set(r) == {'handle', 'kind', 'position', 'revision', 'state', 'physical_revision'} for r in data['received_entries'])
        assert all(not (r['state'] & 4) or r['physical_revision'] == 0 for r in data['received_entries'])
        if data['phase'] == 1:
            assert data['container_count_before'] == 0
            assert data['private_revision_absent']
        if data['phase'] in (5, 6):
            assert data['broken_proxy_clear']
        received.append(path.name)
    assert len(received) == 14, received
    motion = []
    for path in root.glob('[AB]_Motion.csv'):
        rows = [r for r in csv.DictReader(path.open(encoding='utf-8-sig')) if r['phase'] == '8']
        if rows:
            start = [float(rows[0][n]) for n in ('x_cm', 'y_cm')]
            motion = [math.dist(start, [float(r[n]) for n in ('x_cm', 'y_cm')]) for r in rows]
    assert motion and max(motion) >= 150, 'Carried client did not perform the native walk'
    for phase in (1, 3, 7):
        values = [json.loads((root/f'{role}_{phase}.json').read_text(encoding='utf-8-sig')) for role in ('A', 'B')]
        assert sum(v['receipt_result'] == 1 for v in values) == 1, phase
    digest = hashlib.sha256((root/'Objects.json').read_bytes().replace(b'\r\n', b'\n')).hexdigest()
    result = dict(stage='B91', increment='fixed furniture, finite storage and cargo' if report.get('furniture_scenario') else 'finite storage and crate/barrel cargo',
                  full_stage_acceptance=False, native_report_sha256_normalized_lf=digest,
                  checks=len(report['checks']), fitted_objects=size, finite_initial_items=finite_count,
                  independent_owners=2, received_reports=received,
                  limitations=['Other B91 systems and whole-stage acceptance require their own evidence.',
                               'This is a native editor server with two independent headless owners; no packaged server, video or human acceptance.'])
    (root/'ObjectsSummary.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f"PASS B91 objects: {len(report['checks'])} native checks, {size} fitted objects, {finite_count} finite items, two remote owners")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    check(parser.parse_args().output)
