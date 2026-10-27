#!/usr/bin/env python3
"""B90 stage-owned exit gate. Never promote later gameplay dependencies."""
import csv
import hashlib
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

sys.dont_write_bytecode = True
from CheckB90Structure import check as structure
from CheckB90Closure import check as closure
from CheckB90Traversal import verify as traversal
from CheckB90River import check as river
from CheckB90Approaches import check as approaches
from CheckB90Evidence import check as map_evidence
from CheckEconomyEvidence import verify as economy

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def check(directory):
    raw = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(raw)
    assert source['fitter_revision'] == 3
    structure(directory/'structure')
    closure(directory/'closure')
    economy(directory/'closure/legacy/Ledger.json')
    traversal(directory/'traversal')
    traversal(directory/'approaches')
    approaches(directory/'approaches')
    river(directory/'river')
    map_evidence(directory/'map', compare=directory/'map/second', bridge_entry=directory/'bridge_entry')
    first, saved, second = (directory/'map'/p for p in ('first', '.', 'second'))
    assert (first/'FittedGeometry.txt').read_bytes() == (saved/'FittedGeometry.txt').read_bytes() == (second/'FittedGeometry.txt').read_bytes()
    assert read(first/'NativePaths.json')['fitted_pads'] == read(saved/'NativePaths.json')['fitted_pads'] == read(second/'NativePaths.json')['fitted_pads']
    captures = ('capital', 'riverport', 'oakmere')
    for name in captures:
        structure(directory/'captures'/name)
        assert (directory/'captures'/name/'Settlement.png').stat().st_size > 1000
    rules = read(directory/'NativeRules.json')
    assert rules['succeeded'] >= 51 and rules['failed'] == rules['notRun'] == rules['inProcess'] == 0
    assert 'Result: Succeeded' in (directory/'BuildEditor.log').read_text(encoding='utf-8-sig')
    assert 'Result: Succeeded' in (directory/'BuildGame.log').read_text(encoding='utf-8-sig')
    trips = read(directory/'traversal/Traversal.json')
    assert {s['id'] for s in source['settlements']} <= {t['to'] for t in trips['trips']}
    heart = next(t for t in trips['trips'] if (t['from'], t['to']) == ('antechamber', 'heart'))
    assert heart['walk_seconds'] <= 15
    contacts = read(directory/'river/River.json')
    assert next(c for c in contacts['cases'] if c['id'] == 'heart_dry_circle')['wet_samples'] == 0
    heat = read(directory/'traversal/TravelHeatSummary.json')
    assert heat['payload_sha256'] == hashlib.sha256(raw).hexdigest() and heat['journeys'] == 31 and heat['heat_cell_m'] == 50
    cells, prior = defaultdict(float), {}
    for row in csv.DictReader((directory/'traversal/TraversalTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
        t, x, y = (float(row[k]) for k in ('elapsed_s', 'x_cm', 'y_cm'))
        if row['trip'] in prior:
            pt, px, py = prior[row['trip']]
            if t > pt:
                cell = math.floor(((x+px)/200+1200)/50), math.floor(((y+py)/200+1200)/50)
                cells[cell] += t-pt
        prior[row['trip']] = t, x, y
    assert abs(sum(cells.values())-heat['weighted_walk_seconds']) < .01
    assert abs(max(cells.values())-heat['hottest_cell_seconds']) < .01
    for ext in ('png', 'svg'):
        assert (directory/'traversal'/f'TravelHeat.{ext}').stat().st_size > 1000
    details = {
        'T01': 'Three real B90 worlds; repeat import/registry, stable IDs, no duplicate or carried sources',
        'T02': '2,559 native required-target path queries complete',
        'T03': '55 native river/deck/contact fixtures pass',
        'T04': '31,909 ground samples, <=25% grade and corroborated bridge entry',
        'T05': 'Seven targets, 23 local walks and distinct native alternatives',
        'T06': f"Antechamber to dry Heart: {heart['walk_seconds']:.3f}s",
        'T07': '11 settlement identities; 36 service/building seats; six districts per city; civic core/edge mapping',
        'T08': 'Two independent generations and reopen have identical canonical geometry/pads',
        'T09': '144 shells; 216 door capsule sweeps; 108 closed interiors rejected; 72 actual walks; no road/wall overlap',
        'T10': '15 native perimeter/terrace walking, dash and push cases',
        'T11': '31 measured journeys; separate estimates; native gap trace and time-weighted heat plot',
        'T12': 'Same-process v08 switch; original three markets and source ledger replay alone',
    }
    summary = dict(stage='B90', stage_owned_spatial_exit='Passed', payload_sha256=hashlib.sha256(raw).hexdigest(),
                   cases={f'B90-{k}': dict(stage_owned_pass=True, evidence=v) for k, v in details.items()},
                   full_gameplay_acceptance=False,
                   later_stage_dependencies={
                       'B91': 'Interactive doors/interiors/stairs/props, unit visibility, ledges/concealment and mutable authority',
                       'B92': 'Service transactions, guards/population, five markets and full-profile source accounting',
                       'B93': 'Earned full-map victory and claim integration',
                       'B94': 'Full-session multiplayer stress and human pacing'},
                   open_design_risks=['339-second geometric POI-anchor gap; functional-density fitting remains',
                                      'Inspected in-range key-target rays have no geometry cover; range failure is not cover'])
    (directory/'StageSummary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print('PASS B90 stage-owned spatial exit: 12 cases; B91-B94 gameplay dependencies explicitly open')


if __name__ == '__main__':
    check(Path(sys.argv[1]))
