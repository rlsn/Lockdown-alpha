# B91 — approved ladder and rope links

28 October 2026 · source commit 02a9c829cd18a25e15b5908f7ff62175eb760f4f · B91 remains in progress

This checkpoint adds the 12 finite world ladders and all 16 approved paired
anchors. It closes the ladder race in T04 and the bounded link behavior in T09.
The world now contains **288/480 functional sources**: 72 doors and 216 other
props. The 16 anchor records are additional geography, not extra functional
props. The object director holds 232 records and the same 132 physical items.
Remaining functional sources are 64 saplings, 96 grass patches and 32 furniture
instances. The 3,000 scenery-tree targets remain separate open work.

## Runtime behavior

A ladder remains one world instance at weight 10, including during carrying,
deployment and retrieval. Its ground footprint is 70 × 500 × 30 cm. It is neither
a bag item nor a walk/sight obstacle. Carrying uses the existing one-prop,
natural-capacity and 70% speed rules. Deployment accepts only an observed vacant
approved anchor. Upper-end retrieval can safely drop the same ladder onto nearby
ground rather than returning it to its distant initial source.

Rope attachment requires the exact owned Standard Rope in an accessory binding,
current item/inventory revisions, item permission including Mute, six quiet
seconds and a 1.5-second hold. Attachment clears every reference to that binding
and moves the original item into the anchor. It consumes no item and leaves no
bag copy. Quiet retrieval checks the deployer and bag capacity, then recovers
the same unbound instance. No exact item ID or deployer identity is transmitted
in the observed world record.

Either body may use an installed link. Use is a two-second held world action,
without an accessory cast or a fresh quiet requirement. It checks both world
interaction and movement permissions; Root/Stun interrupt it, while Mute does
not turn a placed link into an item cast. The server reserves one user, enforces
the three-second reservation ceiling, and revalidates life, damage, input,
revisions, collision and destination occupancy. Retrieval cannot remove an
occupied link. Other attacks, skills, item activations, movement orders and
services remain suppressed during traversal.

The body follows a swept outside vertical segment and a horizontal portal
segment; it never cuts diagonally through the wall. The server disables native
walking during that interval and sends a sequence/life-bound pose stream to the
owner. Reliable start/end messages stop stale native orders and restore walking.
Interruption returns to a supported clear endpoint. If both endpoints and nearby
return pads are occupied, the body stays movement-disabled and retries a safe
return after releasing the anchor reservation. That saturated case still needs
density stress evidence; it never authorizes displacement or a falling fallback.
End-life cancellation runs before property drops and unpossession.

## Fitting and observation

The immutable source upper points sit beside the existing stair opening. The
compiled overlay fits them to each building's north upper landing: lower offset
`(950,740,0)` and upper offset `(560,740,400)` cm. Both remain within 10 m XY of
their authored points. The upper east wall receives a 3 m-wide, 2 m-high opening;
the ground wall, original sight windows and permanent stairs remain functional.
Each pair is admitted only when native floor traces find its supported rise.

The observed object record gains one field, `Endpoint`, for the other public
static endpoint. The presented anchor position is the nearer endpoint. Ordinary
objects carry zero in that field. The payload exposes neither the current user,
reservation owner, deployer, inventory nor contents. Interaction still requires
a fresh observed handle; knowing a static coordinate grants no request authority.
Installed state is an observed revision, and unchanged snapshots do no proxy work.

## Verification and retained limits

Portable native evidence and the source/input/asset audit are under
[B91Traversal](evidence/B91Traversal/Audit.json). Historical door, storage and fuel
evidence remains unchanged in its original bundles.

| Check | Result |
| --- | --- |
| Closed importer | Six reproducibility/rejection tests; exact 12 ladders and 16 pairs |
| Native link scenario | 55 checks; two independent remote headless owners |
| Fitting | All 12 full ground footprints; all 16 supported endpoint pairs and bidirectional capsule routes |
| Actual owner movement | Ladder ascent/descent and placed rope use; received position traces preserve the two-second movement |
| Concurrency and identity | Single-winner ladder pickup and link-use races; exact deploy/retrieve; duplicate attach/detach replay |
| Interruption | Root, Stun, missing heartbeat, blocked destination before/during use, occupied retrieval and actual end-life/return |
| Conservation | 132 original physical items and 12 original world ladders; no extra source or bag copy |
| Regressions | Storage/cargo 49; portable fuel 32; structural 75 with 52 actual walks; independent door protocol; 51 native rules |
| Builds | Editor and Game Development, full unity |

The Rope binding in the link fixture is explicitly staged using an existing
finite item and one accessory reference. The separate portable-tool regression
proves actual binding RPC and quiet preparation. Fifteen additional anchors use
native floor/capsule route checks; they do not claim separately recorded human
traversals. This is editor-server evidence, without rendered link acceptance,
packaged dedicated-server support, human pacing or population stress. Socket
fire, paid full-map fields/flasks, foliage/concealment, furniture and full-density
fitting remain B91 work.

Owner motion checks retain the raw samples and allow 10 cm for the last frame's
CharacterMovement floor correction. Authority-side landing checks independently
require a supported body within 5 cm of the endpoint.

```powershell
python -B Scripts/SyncB91Traversal.py --check
python -B Scripts/TestB91TraversalImport.py
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Traversal -Port 7925
& Scripts/RunB91Network.ps1 -Objects -Port 7921
& Scripts/RunB91Network.ps1 -Tools -Port 7923
```

Prototype controls: hold **C** to carry a ground ladder, **O** to deploy at a
nearby approved anchor, **H** to attach the selected bound Rope, **F** to use a
placed link, and **K** to retrieve your unused link. Existing bind/selection,
release, movement and Escape cancellation controls still apply. These shortcuts
remain development UI, not the final interaction-selection design.
