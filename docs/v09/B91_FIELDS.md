# B91 — bounded paid full-map fields

28 October 2026 · source f51a01adc8573816f4a4399326add53254c14d8a · B91 remains in progress

This increment connects owned Water/Oil Flasks and inherited Frost Capsule
dispatch to the full-map environment profile. It preserves the existing item
authority, binding, Mute, windup, consumption and family cooldown rules. Water
and Oil are paid native scenarios; Frost is a diagnostic transition/contact
scenario. Full-map Frost acquisition, direct-hit Frost reactions, socket ignition
and broader spell integration remain open.

The development profile remains `v09_environment` on `L_V09Full` with
`LDB91GameMode`. It initializes no population, markets or Heart victory executor.
The 384/480 fitted functional sources, 328 initial object/anchor records and
132 initial physical items are unchanged. New fields are temporary allocations,
not additional world endowments.

## Release and support

Schema 8 wire version 5 explicitly admits full-map XY coordinates and supported
height bands. The environment profile requires this version. The v0.8 wire
version 4 retains its original coordinate bounds and codec. Item requests still
carry the physical identity, current inventory/item/build revisions, actual
accessory source, catalog digest, match, life and token.

N releases the selected bound Water/Oil Flask or Frost Capsule at the cursor.
I selects a bag item; held 1–4 prepares an accessory through the existing quiet
two-second loadout transaction. An unbound item cannot bypass preparation.
Water/Oil retain 400 cm range, 180 cm radius, 250 ms windup and 12-second family
cooldowns. Their field durations remain four and six seconds respectively.
Frost Capsule retains its inherited wet-substrate requirement and cooldown.

The 200 cm grid now uses signed full-map cells. Native support queries use the
selected local height instead of tracing every action from an absolute small-map
ceiling. The release rechecks range, support, sight and eligible transitions;
walls and unsupported steps clip the selected footprint. Full-map substrate is
actual river water or ordinary ground. The old arena's test garden, metal apron
and fuel socket are not inferred from matching XY coordinates in this map.

Each active cell retains a supporting height and normal. Contact follows that
local plane on slopes rather than comparing every foot position with the cell
centre height. Ground contact and periodic surface damage require feet on that
support. Simultaneous fields on stacked
floors at the same XY are not implemented: a conflicting support height refuses
the entire new release before cost. This is an explicit delivery restriction,
not stacked-floor acceptance. River crossing coverage remains in the earlier
B90 spatial fixtures; this scenario exercises a dry, supported 400 cm terrain
band at X=80,000/Y=−80,000 cm.

## Bounded allocation and observation

The environment profile uses 1,024 match cells, 64 cells per sponsor and 2,048
transition evaluations per 100 ms. Legacy profiles keep 256/64/512. The existing
per-cast masks, lifetime caps, nonrecursive transitions and reservation accounting
remain shared. A release with no eligible cells, a conflicting support or
exhausted capacity retains the physical item and starts no family cooldown.
Oil on Wet ground therefore refuses; Oil on dry ground remains Oil without
automatic ignition.

The canonical pooled field actors do not replicate. Authority sends each owner
only observed `Position`, `Revision` and `States` records, filtered by current
18 m sight, Blind and geometry. The view is bounded to 384 entries and expires
locally after 500 ms without a valid update or on match/life change. It contains
no caster, original life, item, cast identity or exact expiry. Client surface
tiles have no collision or navigation authority. Headless evidence validates
the received records; the prototype colors still need rendered readability and
non-color cues.

## Native evidence

[The audit](evidence/B91Fields/Audit.json) pins the committed source, logs,
received client records and unchanged source-package/map hashes.

The eight-phase scenario passes 33 checks with two independent remote clients,
real RPCs and actual clocks. Fixture setup transfers three existing finite flasks; it mints
none. It checks unbound refusal, actual quiet preparation, Mute refusal, paid
Water, duplicate replay, Wet contact, refused Oil on Wet, paid Oil after expiry,
match-cap rejection without spending and a successful retry after capacity
expires. Exactly three consumed identities leave 129 conserved physical items.

Capacity setup deliberately allocates 1,024 terrain-supported diagnostic cells
under detached sponsors with at most 64 each. It is an allocator fixture,
not 1,024 normal earned player actions or B94 performance evidence. The later
Frost transition is also diagnostic, without granting a Frost Capsule item:
wet ice slows contact by 30%, retains Wet and applies no Stun. A conflicting
stacked support request rejects atomically. An actual native ramp checks contact
where the feet differ by more than 12 cm from the cell-centre height. A distant client receives no live
field registry.

The retained regression bundle also reruns saplings, furniture, cargo/storage,
optional traversal, portable fuel, structural walks, independent doors, native
rules and the v0.8 field scenario against this source. Exact counts and build
paths are retained in the audit. Earlier checkpoint evidence is unchanged. Empty book-drop cleanup now exits
before querying a legacy fallback floor; the structural scenario verifies
normal shutdown after all walks and door cases.

| Verification | Result |
| --- | --- |
| Paid full-map fields | 33 native checks; two remote owners; 129 conserved items after three consumes |
| Rules | 53 passed, including the explicit wire, schema and sloped support checks |
| Existing B91 scenarios | Saplings 28; furniture 51; storage/cargo 49; traversal 55; portable fuel 32 |
| Structure and doors | 75 checks, 52 actual walks with zero falling; independent two-owner door race/privacy scenario |
| Legacy fields | B84 at 100 ms RTT / 10 ms jitter; 536 server and 40 checks per client |
| Legacy source replay | 414 receipts, nine accounts, 323 unique items and 4,482 gold reconciled |
| Builds | Editor and Game Development; full unity with adaptive unity disabled |
| Frozen inputs | All 148 saved assets and 59 supplied files retain their original hashes |

```powershell
& Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
& Scripts/RunB91Network.ps1 -Fields -Port 7931
& Scripts/RunB84Check.ps1 -Port 7933
& Scripts/RunTests.ps1
& Scripts/BuildGame.ps1 -FullUnity -MaxParallelActions 4
```

## Remaining acceptance

B91-T05 gains the Water/Oil accessory boundary. T12 remains partial because
direct Frost, guard, victim lock and death ordering need full-map paid evidence;
diagnostic ice contact does not establish them. T14 remains partial because
socket ignition and paid source reservations are still open. Expanded field
capacity still requires B94 stress evidence. Sapling burning/ranged damage and
saved-match state, grass/concealment, tagged ledges, 3,000 scenery trees, the
complete Riverport encounter and human density/readability checks remain B91
work. This checkpoint does not admit B92 or declare B91 complete.
