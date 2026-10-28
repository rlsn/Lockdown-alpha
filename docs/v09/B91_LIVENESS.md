# B91 — issued-command liveness

28 October 2026 · native source `c069e261c1bb1458dab1d4c94759e51830d38e70` · B91 remains in progress

The [source and pointer control checkpoint](B91_CONTROLS.md) left a rendered
harvest failure unresolved. A diagnostic reproduction now identifies its cause:
the command heartbeat was 351.784 ms old against a 350 ms expiry, with zero movement
and a 16.510 ms authority frame. The cancellation came from command liveness.
The failed diagnostic is retained separately from the passing evidence.

## Command behavior

Current UI profiles use a common two-second liveness window for issued orders.
The owning controller refreshes the active command independently of key or mouse
state. A short rendering/network stall can therefore leave a legitimate channel
running. At two seconds without refresh, the order expires and releases its
reservations. A late heartbeat cannot recreate the cancelled action.

The common helper covers world objects, doors, navigation, preparation, production,
channeled skills, tactical item/travel channels, POI activation and Heart claims.
Frozen non-UI hold profiles retain their 250 ms contract. Tactical millisecond
channels compare heartbeat and sample times at the same precision. Completion
checks cannot use a rounding epsilon to finish a two-second action after its
unrefreshed order has expired.

S, focus/lifecycle cancellation and each action's movement, damage, control,
source, range and eligibility checks remain immediate. In particular, Heart
claims retain their distinct tactical interruption rules; this timeout does
not add damage interruption. One press still issues a command and release
still leaves it running. No new binding, free interaction, duration, resource,
fuel cost or prepared slot is introduced.

An additional earned-route regression exposed automatic POIs stopping a move
when the character passed through their radius. Automatic actions now require
the character to stand still with no pending or active movement order. Presence
is still tracked while moving, preserving the leave/re-enter latch after S.
An explicit pickup/service click keeps its approach behavior. The failed first
journey is retained as diagnosis beside the subsequent route evidence.
The inherited route probe also synchronizes its request counter with real
controller orders, preventing automatic POI commands from colliding with the
fixture's next request. This changes no progression, timing or authorization gate.

The compiler now includes the version tag
`:source-click-channel-v2-liveness2` in the active world contract hash, fencing
requests from the previous command contract. The forestry manifest and its eleven finite knives are unchanged:
339 initial records, 143 physical items and 395/491 functional sources.

## Native evidence

[The frozen bundle](evidence/B91Liveness/README.md) pins the native source, raw
reports and normalized source/evidence hashes. Its 148 saved assets and 59 supplied
release files retain their original physical-byte hashes.

| Check | Result and scope |
| --- | --- |
| Rendered source/pointer scenario | 47 checks; one 720p offscreen rendered owner and one headless peer |
| Brief heartbeat gap | Real command sender pauses for one second; authority observes a gap over 500 ms and harvest finishes once |
| True timeout | No refresh for two seconds cancels before harvest; source/knife retained, reservation released, late pulse cannot restart |
| Optional traversal | 55 checks, 16 paired routes; missing heartbeat returns to a supported endpoint and releases the link |
| Native rules | 55 pass, including strict liveness boundary and legacy-profile isolation |
| Shared input / inventory | 53 / 111 assertions across authority and two clients |
| Production | 50 local-authority checks and independent receipt replay; controlled clock |
| Inherited field workload | 616 assertions, with independent conserved-ledger replay |
| Earned Heart routes | 386 assertions and 141 native journeys across Keys, Siege, Artifact and Charter; independent source reconciliation |
| Builds | Editor and Game full-unity Development targets pass |

The liveness suites ran before the final stationary-POI adjustment. Earned
routes and shared input were rerun after that adjustment and fixture sequencing
fix. The B91 environment controller bypasses the shared automatic-POI method;
its rendered source evidence exercises the unchanged environment command path.
The final Editor/Game builds contain the complete increment.

Reproduction commands, run sequentially from the project root:

```powershell
./Scripts/BuildEditor.ps1 -FullUnity -MaxParallelActions 4
./Scripts/RunTests.ps1
./Scripts/RunB91Network.ps1 -Saplings -Capture -RenderedClient A -Port 7961 -TimeoutSeconds 350
./Scripts/RunB91Network.ps1 -Traversal -Port 7962
./Scripts/RunInputCheck.ps1 -Port 7966
./Scripts/RunInventoryCheck.ps1 -Port 7964
./Scripts/RunProductionCheck.ps1
./Scripts/RunB84Check.ps1 -Port 7965
./Scripts/RunWorldCheck.ps1 -Profile v08_integration -Route All
./Scripts/BuildGame.ps1 -FullUnity -MaxParallelActions 4
```

The final timeout was observed at 2.007622 s heartbeat age with zero movement.
The small overshoot is the next server sampling frame. The screenshot was
visually inspected: current Forestry Knife binding, source command buttons,
Stop [S] and owner harvest progress are visible. It establishes a captured
layout; it does not establish human readability, real input usability or a
frame-rate budget. The fixture invokes actual player command methods and
stages existing finite items for binding/capacity cases.

The older failed rendered sample in B91Controls remains historical evidence.
Its limitation is superseded only by this explicitly bounded passing scenario.
B91 still needs grass/concealment, saved mutations, broader paid actions,
layered fields and full-map fitting. Population/economy, earned full-map victory
and full-session/human acceptance remain later-stage work.
