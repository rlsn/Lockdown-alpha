# Continuation checkpoint - 18 October 2026

Current stage: B33 observation foundation. See B33_CHECKPOINT.md and TestReport.md
for implementation boundaries and the latest executed evidence. B34 combat/life
is next, after confirming the B33 checks on the current build.

## Current architecture

Server-only contender records and vision director; owner-only canonical body,
PlayerState, private controller state and relay; nonreplicated role-neutral
observed proxies. Sight unions body, owned wards and a shared public sensor.
Full views replace state, reliable concealment is revisioned, and a 250 ms
receive gap clears dynamic proxies. Fog and minimap distinguish current,
explored and unknown cells. Camera adds no source.

Six native rule/view tests, the movement probe and the three-process network
probe are the repeatable checks. Generated content has 23 real packages and
12 authored meshes per map. Validate also checks sight/aim collision and the
plateau's logical-elevation tag. OpenStandalone.ps1 provides an interactive
uncooked game window; the normal editor retains MCP plugins.

## Next stage

B34: basic attack, compatible dash, warned assassination, simultaneous damage,
death, retained loss/return/protection and fresh body identity. Resolve observed
IDs through the requesting connection and recheck authority sight/range. Carry
match/life generations in requests. Route secondary effects through the same
view boundary; do not multicast canonical body references or private stats.
Attack-driven ward destruction is part of this work. Then B35 courier/Seal/Heart
transactions and B36 cooked independent-client, delay and performance evidence.

## Workflow

Close the editor for reflected/native changes. BuildEditor.ps1 and BuildGame.ps1
verify separate targets. RunTests.ps1, RunMovementCheck.ps1 and
RunViewNetworkCheck.ps1 write ignored local evidence. Keep a runnable checkpoint.
Validate does not write packages; Regenerate is explicit. Hand-authored assets
belong outside /Game/Lockdown/Generated/. The web baseline stays unmodified at
74d3d3e884bf4d8130e1596148b45e50a409039c.

The owner's EngineAssociation=5.8 and normalized .vsconfig are committed in
77305e0. The generated config was corrected to the tested MSVC14.44/SDK26100/
FrameworkSDK4.8 prerequisites, removing the earlier banned 14.50 recommendation.
Ignored Config/Local/Environment.json contains this machine's engine path.
