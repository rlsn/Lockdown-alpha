#!/usr/bin/env python3
"""Render measured native travel corridors and time-weighted spatial heat."""
import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.colors import LogNorm
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def plot(directory):
    raw = (ROOT/'Content/Lockdown/Data/V09/map_b90.json').read_bytes()
    source = json.loads(raw)
    matplotlib.rcParams['svg.hashsalt'] = hashlib.sha256(raw).hexdigest()
    report = json.loads((directory/'Traversal.json').read_text(encoding='utf-8-sig'))
    assert report['complete'] and not report['failures'] and report['lane'] == 'All' and len(report['trips']) == 31
    assert report['payload_sha256'] == hashlib.sha256(raw).hexdigest()
    traces = defaultdict(list)
    for row in csv.DictReader((directory/'TraversalTrace.csv').read_text(encoding='utf-8-sig').splitlines()):
        traces[row['trip']].append(row)
    segments, empty, midpoints, weights = [], [], [], []
    for rows in traces.values():
        for a, b in zip(rows, rows[1:]):
            delta = float(b['elapsed_s'])-float(a['elapsed_s'])
            if delta <= 0:
                continue
            p, q = ([float(r['x_cm'])/100, float(r['y_cm'])/100] for r in (a, b))
            segments.append([p, q])
            if min(float(a['distance_to_poi_cm']), float(b['distance_to_poi_cm'])) > 5000:
                empty.append([p, q])
            midpoints.append(np.mean([p, q], axis=0)); weights.append(delta)
    assert abs(sum(weights)-sum(t['walk_seconds'] for t in report['trips'])) < .01
    xy = np.asarray(midpoints)
    heat, _, _ = np.histogram2d(xy[:, 0], xy[:, 1], bins=48, range=[[-1200, 1200], [-1200, 1200]], weights=weights)
    fig = plt.figure(figsize=(14, 10), facecolor='white', layout='constrained')
    grid = fig.add_gridspec(2, 2, height_ratios=[3.4, 1.3])
    left, right, bars = fig.add_subplot(grid[0, 0]), fig.add_subplot(grid[0, 1]), fig.add_subplot(grid[1, :])
    for ax in (left, right):
        ax.set(xlim=(-1200, 1200), ylim=(-1200, 1200), aspect='equal', xlabel='East (m)', ylabel='North (m)')
        ax.grid(alpha=.15, linewidth=.5)
        for river in source['rivers']:
            p = np.asarray(river['points'])/100
            ax.plot(p[:, 0], p[:, 1], color='#4b9bbe', linewidth=1.3, zorder=2)
    left.add_collection(LineCollection(segments, colors='#7c878f', linewidths=.7, alpha=.6, zorder=3))
    left.add_collection(LineCollection(empty, colors='#c93932', linewidths=2, alpha=.9, zorder=4))
    left.set_title('31 actual journeys\nRed: sampled corridor with no POI anchor within 50 m', loc='left', fontsize=11)
    positive = heat[heat > 0]
    image = right.imshow(np.ma.masked_where(heat.T == 0, heat.T), origin='lower', extent=(-1200, 1200, -1200, 1200),
                         cmap='magma_r', norm=LogNorm(vmin=max(.01, positive.min()), vmax=positive.max()), zorder=1)
    fig.colorbar(image, ax=right, fraction=.046, pad=.035, label='Summed fixture walking seconds per 50 m cell')
    right.set_title('Time-weighted traversal heat\nFixed placements; aggregate fixtures, not one match', loc='left', fontsize=11)
    names = {p['id']: p.get('name', p['id']) for p in source['pois']}
    for ax in (left, right):
        for s in source['settlements']:
            x, y = np.asarray(s['position'][:2])/100
            ax.scatter([x], [y], s=22 if s['kind'] != 'town' else 10, facecolor='white', edgecolor='#16323f', linewidth=.8, zorder=5)
            ax.annotate(s['name'], (x, y), xytext=(4, 5), textcoords='offset points', fontsize=7, color='#16323f', zorder=6)
    worst = sorted(report['trips'], key=lambda t: t['longest_empty_s'], reverse=True)[:8][::-1]
    labels = [names[t['from']]+' → '+names[t['to']] for t in worst]
    values = [t['longest_empty_s'] for t in worst]
    bars.barh(labels, values, color='#c93932', height=.65)
    for i, value in enumerate(values):
        bars.text(value+3, i, f'{value:.1f} s', va='center', fontsize=8)
    bars.set(xlabel='Longest continuous interval without a POI anchor within 50 m (native per-tick measurement)', xlim=(0, max(values)*1.15))
    bars.tick_params(axis='y', labelsize=8); bars.grid(axis='x', alpha=.15)
    fig.suptitle('B90 measured travel — geometry and empty-corridor evidence', fontsize=15, fontweight='bold')
    fig.supxlabel(f"Payload {report['payload_sha256'][:16]}… · 420 cm/s · {sum(weights):,.3f} summed walking seconds\n"
                  'Heat uses elapsed-time weights between trace samples. POI gaps exclude future props/encounters; no human pacing or full-session claim.', fontsize=9)
    for ext in ('png', 'svg'):
        metadata = {'Date': None, 'Creator': 'Lockdown native travel plot'} if ext == 'svg' else {'Software': 'Lockdown native travel plot'}
        fig.savefig(directory/f'TravelHeat.{ext}', dpi=180, metadata=metadata)
    plt.close(fig)
    summary = dict(payload_sha256=report['payload_sha256'], journeys=31, heat_cell_m=50,
                   weighted_walk_seconds=sum(weights), hottest_cell_seconds=float(heat.max()),
                   longest_native_poi_gap_seconds=max(values), sampled_empty_segments=len(empty),
                   matplotlib_version=matplotlib.__version__, numpy_version=np.__version__,
                   scope='Derived from actual native traces; geometric POI-anchor coverage only; no functional-density or human pacing acceptance')
    (directory/'TravelHeatSummary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('directory', type=Path)
    plot(parser.parse_args().directory)
