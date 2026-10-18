# Continuation checkpoint - 18 October 2026

Current stage: B34 combat and life. See B34_CHECKPOINT.md and TestReport.md for
implementation boundaries and executed evidence. B35 transactions are next.

## Current architecture

Server-only contender records and vision director; owner-only canonical body,
PlayerState, private controller state and relay; nonreplicated role-neutral
observed proxies. Sight unions body, owned wards and a shared public sensor.
Full views replace state, reliable concealment is revisioned, and a 250 ms
receive gap clears dynamic proxies. Fog and minimap distinguish current,
explored and unknown cells. Camera adds no source.

Seven native rule/view/combat tests, movement and combat world probes, and the
three-process view/combat network probes are repeatable checks. Generated content has 23 real packages and
12 authored meshes per map. Validate also checks sight/aim collision and the
plateau's logical-elevation tag. OpenStandalone.ps1 provides an interactive
uncooked game window; the normal editor retains MCP plugins.

Combat samples after movement, before vision. It owns due impact batches and
resident returns. GameMode owns contender loss/return receipts. Dash is a server
Character Movement root-motion source. Waiting uses a local fog camera; return
replaces pawn and life identity. Private state has a revision guard. The same
seven-part body and status marker render all roles; effects stay inside views.

## Next stage

B35: courier collection/delivery with acknowledged reward, real Seal carry/drop/
pickup ownership, access predicate, ten-second Heart claim and one terminal result.
Acquisition and defence changes remain labelled local-authority fixtures. Commit
noncombat transactions after damage/deaths and before claim/deadline evaluation.
Use connection-scoped observed/public IDs and match/life/request generations.
Never multicast canonical references or private stats. Then B36 cooked independent
clients, complete reset/isolation, broader delay/performance and human readability.

## Workflow

Close the editor for reflected/native changes. BuildEditor.ps1 and BuildGame.ps1
verify separate targets. RunTests.ps1, RunMovementCheck.ps1 and
RunViewNetworkCheck.ps1, RunCombatCheck.ps1 and RunCombatNetworkCheck.ps1 write
ignored local evidence. The combat network runner supports 100/150 ms configured
RTT, +/-25 ms one-way jitter, measured echoes/receipts and an optional rendered client.
Its six action samples per client do not establish a performance distribution.
Keep a runnable checkpoint.
Validate does not write packages; Regenerate is explicit. Hand-authored assets
belong outside /Game/Lockdown/Generated/. The web baseline stays unmodified at
74d3d3e884bf4d8130e1596148b45e50a409039c.

The owner's EngineAssociation=5.8 and normalized .vsconfig are committed in
77305e0. The generated config was corrected to the tested MSVC14.44/SDK26100/
FrameworkSDK4.8 prerequisites, removing the earlier banned 14.50 recommendation.
Ignored Config/Local/Environment.json contains this machine's engine path.
