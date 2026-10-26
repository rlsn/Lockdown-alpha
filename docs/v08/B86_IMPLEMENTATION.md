# B86 native usability and validation

B86 improves the explicit v08 integration profile's native HUD, equipment
inspection, input and packaging workflow. The catalog remains 52 abilities,
80 items, 20 recipes and nine presets, with the B85 world routes retained.
This is an engineering checkpoint with measured runtime evidence; missing
human or inherited acceptance is not a release pass.

## Recorded outcomes

| Check | Result |
|---|---|
| Editor / fresh Win64 Development archive / seven-map validation | Pass |
| Native rules | 48 tests pass |
| Cooked 720p,1080p and720p125% text UI | 22 checks each;12 screenshots |
| Retained cooked local preparation | 61 checks pass |
| Native v08 lease/death/status boundaries | 20/4/5 checks pass |
| B81 / B84 / v07 loadout network regression | 28/17/16;536/40/40;46/27/26 checks pass |
| Cooked 600-second four-owner workload | Server6; A28/B26/C28/D26; zero failures |
| Independent economy replay | Six ledgers pass |
| Human readability / observed TTK and control chains | **Not run** |

The final workload records **4,173** full 256-cell samples, **297**
field batches and **596** reservation/release cycles. The broader service
proxy is **112.80ms p95 / 134.84ms p99** (maximum 151.56ms), against
an 11.26ms baseline p95. **The provisional 2ms/5ms goal remains open.** Whole view
work dominates these observed samples; this is not an isolated incremental-cost
measurement. Authority working-set samples range 358.4–363.0MiB.

The one rendered client averages 60.00FPS at 1280×720, with frame intervals
16.93ms p95 / 17.10ms p99. The three headless clients provide
native tick/network samples, not rendering acceptance. Raw spikes, ages, RTT,
bytes, minute actor/process samples and exact machine/driver metadata are in
[evidence](evidence/B86/Performance.json) and its associated Soak reports/logs.
The four-renderer memory failure and completed earlier run with its failed
startup assertion remain separate, without rewriting their results.

Launch the final local archive:

```powershell
./Scripts/OpenPackaged.ps1 -PackageRoot 'D:\Unreal Projects\Lockdown\Artifacts\20261026-224411-501-PackageWin64\Archive' -Profile v08_integration
```

## Native changes

The six shared actions use a common owner-only presentation model. It separates
learned skills, body-item actives, active accessories, passive accessories,
empty slots and missing/unbound sources. It explains insufficient MP, actual
Silence/Mute/Stun/Hex, reservation, protection, return, terminal state, busy
actions and maximum owner/physical/family reuse endpoints. Major protections
retain their common lock. No target, enemy inventory or canonical field query
is added to this read model. Readiness describes the owned source; range,
targeting and release conditions still apply.

Wrapped source/name/state rows fit six shared actions and four accessories.
The expanded HUD band supports the native viewport and text-scale variants
recorded in the evidence. Controls explain the ten-action limit and the
Wet/direct-Frost/Frozen/Steam distinction. Item inspection uses the current
profile's catalog, explains shared body-action assignment separately from
accessory binding, and displays authored range/radius/lifetime where present.
The old future-B76 objective placeholder is removed. Character attribute previews
use separate natural/effective lines, and shared gear-action labels wrap within
their columns. Manual screenshot review caught those clips beyond the automated
HUD bounds check; the final archive and screenshots include the correction. The enlarged-text
v08 objective banner also reserves enough height for its wrapped context.

Rendered verification exposed a local-host lifecycle bug: direct authority
publication copied OwnerState without running its notification. The first
transaction receipt could therefore appear to be a new life and clear its own
just-accepted hold. Local publication now runs the same notification. That
notification installs the new lifecycle before cancellation can synchronously
publish a nested receipt, and performs cleanup on lifecycle transitions rather
than every already-inactive update. Stale revision rejection remains intact.
The visual fixture starts from a fresh practice and verifies a real held atomic
equipment/action commit before interacting with the HUD.

Packaging now explicitly cooks both L_V07Region and L_V08Region alongside the
existing five maps. The UFS catalog dependencies remain checksum-closed.
BuildCookRun limits compiler workers and disables adaptive unity to verify
ordinary unity compilation. The explicitly flagged B86 packaged listen diagnostic
reserves five transport slots for its nonparticipating local host plus four
remote owners. The gameplay roster remains capped at four and ordinary
launches keep their existing transport limit. OpenPackaged accepts an optional explicit profile;
its default frontend remains unchanged. A fresh archive and manifest identify
the actual package, source state, executable and container hashes.

## Evidence methods

CaptureB86UI runs native rendered UMG and Enhanced Input. Its isolated labelled
fixture supplies knowledge/stats/items/placement, then uses actual held
preparation, remapping with collision rejection, saved/reloaded preferences,
world input, focused Slate Escape and native panels. Temporary preference slots
are isolated from normal settings. Screenshots cover HUD, Inventory, Character
and Controls; automated input is not human or OS-device acceptance.

RunB86Check waits for the actual replicated schema8 six-action/eleven-binding
owner model with a ten-second bound and rechecks it at completion. A reliable
phase RPC can arrive before those properties; failed premature assertions are
retained separately. It starts four independent owning clients, with four actual contender
budgets, a separate authority, normal clocks and repeated native navigation.
Its labelled workload queues bounded Water/Burning/Smoke commands as one
authority batch, then commits once, matching the ordinary boundary path. Expiry and
reservation/release cycles at the 256-cell ceiling. The final block uses grid x36–51/y20–35,
separate from City. Each actual owner sponsors 64 cells; clients use native
paths near Guild with distinct offsets. The batch is diagnostic, so it is
labelled separately from the B85 earned routes. It does not represent earned
acquisition or paid spell input. Rendered-client count, hardware, driver,
resolution, latency settings and process memory are recorded. The accepted
run has one rendered client and three headless clients because four renderers
exhausted available commit. City navigation is not a worst-case 256-visible-field
rendering test or a separate-machine distribution test.

After a 30-second baseline, approximately 100ms wall windows accumulate observed
whole authority/view sample costs plus fixture field-command costs. These sums
are broader than maintenance-only timing and are not a precise isolation of
added environment cost. Transport and native client frame intervals are
recorded separately. Reports retain raw samples, spikes, snapshot age, measured
RTT, connection byte counts and minute actor/memory samples. CheckB86Evidence
independently recomputes nearest-rank percentiles and verifies the five reports,
duration, lifecycle work and cooked-runtime marker where required. Results and
the provisional 2ms/5ms comparison are recorded without a universal guarantee.

Exact final outcomes, commands and artifact directories are in
`evidence/B86/Summary.json`. Earlier failed fixture attempts remain local
Artifacts and are not counted as passes.

## Remaining acceptance

Human participants, observed fight TTK/control chains, accessibility and real
device/OS-scale interaction must be recorded separately. See B86_PLAYTEST.md.
Per-definition independent-client/adverse combinations, proc transfer/death
races, prior-life/protection/hidden-chain combinations and original named
deferrals remain explicitly mapped. A passing current v07 regression does not
root-cause its historical intermittent held-preparation failure. The original
E01–E48, V07-D01–D08 and V06-D01–D08 mappings remain preserved.

No frozen specification/data/template/archive, historical payload/evidence or
map geometry changes are required by this checkpoint. No new terrain/nav
mechanic, old-executable compatibility, live-match migration or reconnect
mechanism is claimed.
