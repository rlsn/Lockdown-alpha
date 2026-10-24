# B71 — combat core and MP

24 October 2026. Native implementation on B70 `91b0563`, preserving the pinned
UE5.8.4 CL58902052 toolchain. This is the **B71 combat slice**, not complete
v0.7. B72–B77 and the release ledger retain their remaining requirements.

## Delivered source and normal acquisition

`Profile=v07_integration` now selects a separate 2100-second match with schema7,
normalized legacy starts, level15 XP/point tables, five shared skill slots and
three usable quick references. `Profile=v06_regression` aliases the existing
V05 economy. Unknown selectors reject; selection is fixed at world creation.
The default frontend still hosts V05. No runtime switch or saved-match conversion.

The server's existing combat director owns HP/MP, action acceptance, release,
damage and life ordering. New tactical requests carry bounded schema/profile7,
catalog digest, match/life, sequence, build revision, slot and observed handle.
The build-plan extension preserves the legacy four-slot path and adds bounded
16-bit skill IDs,38 rank entries/five slots and digest verification for v07.
IDs0–9 remain legacy; only implemented new IDs can enter a normal loadout.

| B71 content | Real effect | Normal source |
| --- | --- | --- |
| Arc Bolt | Stationary300ms cast,22MP; snapshot projectile1200cm/s, original target-life collision, magical damage85/98/110 | Guild40g book, quiet3s read, station equip |
| Anchor Bash | Stationary350ms cast,24MP; physical45 and stun1200/1350/1500ms in one blockable packet | Guild40g book, quiet read/equip |
| Purge Touch | Stationary300ms cast,30MP; basic negative self dispel, positive dispel on another observed body | Guild40g book, quiet read/equip |
| Surety Shell | Stationary250ms cast,30MP; strongest capped general barrier150/173/195 for3s with skill INT scaling | Guild40g book, quiet read/equip |
| Null Bell | Consumed self basic dispel,12s retained owner cooldown; requires one of three quick references | Official Supply8g, unlimited paid creation; no market buyback |

Purge's normal button arms observed-body targeting; pressing the skill again
confirms self use. No aid-consent toggle is supplied in B71, so
another body is never implicitly treated as consenting. External consent/Remedy
content remains later work. Strong dispel and spell-block primitives are real
server rules exercised by labelled fixtures; their full normal-source catalogs
remain B75 scope. No other new ability/item is advertised as working.

The staged [B71 subset](../../Content/Lockdown/Data/V07/b71_combat.json) is copied
from the adopted catalogs by [SyncV07CombatData](../../Scripts/SyncV07CombatData.py).
Bootstrap imports `/Game/Lockdown/Generated/V07/DA_CombatCatalog`, validates its
source checksum and typed fields, and rejects unsupported operation sequences.
Runtime loads that native asset. Existing schema1 character/economy assets and
bounded institutional ticks/budgets remain the regression data. The existing
economy data's1500s declaration describes its retained table, not the new match
deadline; its eight scheduled opportunities still stop at1440s.

## Action, resources and privacy

Arming spends nothing. Accepted windup stops pathing; release rechecks build,
life, equipment, range/sight and MP. Stale/replayed requests do not release twice.
Stop/focus/movement/control cancels before release without MP/full cooldown;
400ms recovery and a cancellation watermark prevent accepting an already
cancelled queued request. Projectile allocation precedes spending. After release,
spell block/miss does not refund MP or cooldown. Legacy economic tasks retain
their zero-MP scores, holds, accepted-channel cooldowns and common slot legality.

v07 derives HP/MP, attack damage and movement from the selected formulas. Regen
uses elapsed integer milliseconds with carried fixed-point remainders; HP1/s,
MP2/s. Preparation clamps current resources rather than filling a larger max;
explicit spawn restore refills. Physical armor/magical resistance cap at60%;
barriers absorb before HP and cap at40% max HP. Due damage is gathered before
death finalization. Legacy Spark is typed magical, Ambush/basic physical.

Stun blocks movement, attack, skills/items and institutional work; silence blocks
skills but permits active items; mute reverses; root prevents voluntary movement/
dash but permits stationary attacks/skills; disarm prevents basic attacks.
External strong dispel removes hard tags; basic dispel cannot remove stun/hex.
Independent source endpoints overlap to their maximum rather than sum. One spell
block consumes its charge before any component of a hostile targeted packet.

Own HUD shows MP, barrier and status/counter text. Remote private builds/MP remain
owner-only. A new observed projectile kind uses the existing eight-field tuple:
an opaque viewer handle and geometry-authorized current position, no target or
caster identity/trajectory endpoint. Native proxies draw a blue projectile cue;
cast and hit cues remain filtered. GameState's34-field public allowlist is unchanged.

## Verification

All focused B71 gates pass; [summary](evidence/B71/Summary.json) records provenance
and [source manifest](evidence/B71/SourceManifest.json) pins final runtime inputs
(LF-normalized text, raw native assets).

| Lane | Result | Evidence |
| --- | --- | --- |
| Pinned Editor build | Passed,4 actions/12.68s, cap4 | [Build log](evidence/B71/BuildEditor.log) |
| Native assets |28 retained packages plus versioned combat catalog generated/validated;0errors,4 existing nav/CrowdManager warnings | [Bootstrap](evidence/B71/Bootstrap.log), [import identity](evidence/B71/DataImport.json) |
| Native rules |26 passed,0failed; includes status legality, fixed-point resources, bounded wire/catalog and privacy schema | [Rules](evidence/B71/Rules.json) |
| Independent clients |34 server/18 client A/15 client B checks,0failures | [Authority](evidence/B71/NetworkServer.json), [A](evidence/B71/NetworkA.json), [B](evidence/B71/NetworkB.json) |
| V05 regression |56 controlled authority checks,0failures | [Regression](evidence/B71/V06Authority.json) |
| Independent journal replay | Both B71 and V05 receipts reconcile; B71 ends467g =450+25−8 | [Replay](evidence/B71/ReceiptReplay.txt), [journal](evidence/B71/Ledger.json) |
| Specification/staged data |931 assertions; staged subset matches adopted catalogs; all93 original spec hashes preserved | [Model](evidence/B71/SpecModel.txt), [import](evidence/B71/DataImport.json) |

Network configuration is100ms RTT with10ms jitter; actual median echo was132.8ms
on each client. This is one measured run, not the full required100/150ms matrix.
The controlled fixture uses real RPCs with diagnostic builds/items/placement;
it is not a normal earned route or human playtest. [Receipts](evidence/B71/NetworkReceipts.txt)
show single releases, consumed spell block and typed damage. Its paid Bell
purchase/consumption uses the real common item/gold journal. Normal scene
rendering, physical input and OS-scale readability are explicitly Not run.

The [working ledger](FeatureLedger.csv) has1 completed source/build audit,
20 scoped partial rows and34 Not run. No whole-release row is upgraded merely
because a core primitive passed. The inherited acceptance matrix stays frozen.

## Remaining milestone boundaries

- B72: full geometry/control emitters, smoke/detection, displacement, explicit
  interrupts and the new Heart interruption contract. Heart still uses retained
  damage/proximity interruption until that route migration is integrated.
- B73: complete equipment/natural-effective split, quiet binding/death migration,
  full normalized Witness journeys and saved D/removed-quick4 preference migration.
  Slot5 currently has native mouse activation; D is not silently assigned. Quick
  storage keeps four legacy-compatible cells, with only first3 usable in v07.
  Respawn still retains the legacy delay/protection pending B73.
- B74/B75: complete acquisition, recipes/quality, all28 new skills/40 items and
  their normal sources. B71's additional Surety Shell supplies a real barrier
  for the core slice. Other catalog entries remain unavailable.
- B76: four routes, simultaneous inclusive claim endpoints and chronological
  stalled-tick event draining. B71 does not claim those late-event guarantees.
- B77: cold cook/package, full privacy/latency matrix, restarts/performance,
  rendered/physical/OS-scale verification, normal sessions and human evidence.
  Inherited V06-D01–D08 stay scoped gaps, not silently upgraded passes.

## Reproduction

```powershell
python Scripts/SyncV07CombatData.py --check
./Scripts/BuildEditor.ps1 -MaxParallelActions 4
./Scripts/BootstrapAssets.ps1
./Scripts/BootstrapAssets.ps1 -Mode Validate
./Scripts/RunTests.ps1
./Scripts/RunTacticalCheck.ps1 -RoundTripMs 100 -JitterMs 10
./Scripts/RunCharterCheck.ps1 -AuthorityOnly
./Scripts/OpenStandalone.ps1 -Profile v07_integration
```

For regression, use `-Profile v06_regression` or the normal frontend. Start a
private lobby/Ready as before. Earn gold through retained work, buy the new books
at Guild and Null Bell at Official Supply. Read/equip through native panels;
assign Bell to a quick reference. No new normal free combat kit is granted.
