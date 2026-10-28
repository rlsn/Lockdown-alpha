# B91 — fixed furniture and private storage

28 October 2026 · source 48f0766bf24e6006cdf8a57213409560cf221a71 · B91 remains in progress

This increment fits the 32 supplied furniture sources and enables their real
storage interaction. Coverage reaches **320/480 functional sources**: 72 doors
and 248 other props. The 16 additional anchor records bring the object director
to 264 initial records. The original 132 finite physical items remain unchanged. The
remaining functional sources are 64 saplings and 96 grass patches; the 3,000
scenery-tree targets and full-map environment integration are still open.

## Behavior and authority

Furniture is a fixed 200 × 100 × 100 cm low fixture. It blocks walking and local
navigation, permits sight, and cannot be carried, pushed or broken. Inspection
opens the existing revision-bound private storage view rather than exposing
contents through the public object snapshot. The prototype uses held F to open,
T to take and Y to store, with the existing item-selection controls.

Each fixture starts empty. It uses the current 24-instance / 60-weight container
limit and the inherited physical-item authority. Open takes 300 ms; exact-item
take/store takes 500 ms and six combat-quiet seconds. Life, current observation,
120 cm reach, control, movement, damage, heartbeat and revision checks still
apply. Competing transfers and duplicate requests cannot create a second item.
An empty fixture implies no free stock, gold or material reward.

The compiler closes the supplied furniture definition, count, dimensions,
walk/sight behavior and verbs. It rejects a sight-blocking furniture definition
or an added furniture endowment. Native fitting stays within 10 m XY of each
supplied POI-anchored source, uses supported surfaces and preserves the existing
door, stair, service, spawn, Heart and road exclusions. Blocking props receive
the full three-metre spacing rule even when they are not heavy cargo.

Furniture is added to the existing object contract. Its appended kind is 12;
prior kinds and equipment definitions retain their IDs. The combined request
checksum changes with that contract. Neither the observed object schema nor
the original 80-item equipment catalog changes. The saved B90 geometry and
supplied specification remain byte-preserved.

## Native evidence

[The audit](evidence/B91Furniture/Audit.json) retains the exact source, input
hashes, reports and raw logs. Earlier checkpoint bundles remain historical.

| Check | Result |
| --- | --- |
| Object import | Six boundary/reproducibility tests; 188 base objects including 32 empty furniture fixtures |
| Furniture scenario | 51 native checks with two independent remote owners |
| All 32 fixtures | Supported legal fit, initial emptiness, walking/nav obstruction and actual unobstructed sight rays |
| Fixed behavior | Carry, push and break reject without mutation |
| Private storage | Both owners legally open; one exact-item take winner; the winning owner stores the same item back |
| Conservation | 132 physical identities remain in exactly one location |
| Regressions | Original storage/cargo 49; links 55; fuel 32; structure 75 with 52 walks; independent door scenario; rules 51 |
| Builds | Editor and Game Development with full unity |

The positive furniture-storage fixture transfers three existing cache goods
from a chest into one initially empty furniture instance. It creates no item,
does not add an endowment, and later transfers two of those same items to the
existing filled-crate break fixture. The separate ordinary storage run retains
the original chest scenario. Native fitting covers the supplied open-surface
POI pads; this is not an additional authored interior-furnishing pass.

Evidence uses an editor authority and independent headless clients. Rendered
furniture review, packaged dedicated-server support, population/density stress,
human pacing and full-stage acceptance remain unproven. Foliage/concealment,
socket fire, paid full-map flask/field/contact integration and scenery fitting
remain B91 work.

```powershell
python -B Scripts/SyncB91Objects.py --check
python -B Scripts/SyncB91Tools.py --check
python -B Scripts/SyncB91Traversal.py --check
python -B Scripts/TestB91ObjectsImport.py
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Furniture -Port 7927
& Scripts/RunB91Network.ps1 -Objects -Port 7921
```
