# Continuation checkpoint - 19 October 2026

Current stage: B36 package and engineering verification. See B36_CHECKPOINT.md
and TestReport.md for exact package paths, measured results and remaining gates.
Human usability review remains distinct from scripted engineering checks.

## Current architecture

Server-only contender records and vision director; owner-only canonical body,
PlayerState, private controller state and relay; nonreplicated role-neutral
observed proxies. Sight unions body, owned wards and a shared public sensor.
Full views replace state, reliable concealment is revisioned, and a 250 ms
receive gap clears dynamic proxies. Fog and minimap distinguish current,
explored and unknown cells. Camera adds no source.

Eight native rule/view/combat/objective tests, movement/combat/objective world
probes and three-process view/combat/objective network probes are repeatable checks.
Generated content has 23 real packages and
12 authored meshes per map. Validate also checks sight/aim collision and the
plateau's logical-elevation tag. OpenStandalone.ps1 provides an interactive
uncooked game window; the normal editor retains MCP plugins.

Combat samples after movement, before vision. It owns due impact batches and
resident returns. GameMode owns contender loss/return receipts. Dash is a server
Character Movement root-motion source. Waiting uses a local fog camera; return
replaces pawn and life identity. Private state has a revision guard. The same
seven-part body and status marker render all roles; effects stay inside views.

## Next stage

Finish any open engineering gate recorded in TestReport.md, then conduct B37's
human comparison of movement, aim, threat warning, fog and interaction feedback
at720p/1080p before expanding gameplay. The installed-engine packaged topology is
a headless listen authority plus two remote clients. A dedicated Server target
remains conditional on a compatible source engine. Full profession/crafting,
tower progression, population and economy remain outside the migrated slice.

B35 transactions run after damage/deaths and before claims/deadline. Acquisition
and defence changes are labelled local-authority fixtures; pickup/drop/theft are
real. One immutable Seal survives corpse cleanup. Courier contracts, receipt
balances and carried inventory are private; public notices contain only district.
Hold refresh is 10 Hz with 250 ms expiry. Route is fixed per claim attempt. Damage
on a completion batch wins over completion; only scheduled claims <= deadline can
win. One receipt freezes competitive activity. No survivor/kill-score winner.

## Workflow

Close the editor for reflected/native changes. BuildEditor.ps1 and BuildGame.ps1
verify separate targets. RunTests.ps1, RunMovementCheck.ps1 and
RunViewNetworkCheck.ps1, RunCombatCheck.ps1, RunCombatNetworkCheck.ps1,
RunObjectiveCheck.ps1 and RunObjectiveNetworkCheck.ps1 write ignored local evidence.
Combat/objective network runners support 100/150 ms configured
RTT, +/-25 ms one-way jitter, measured echoes/receipts and an optional rendered client.
Their small action samples do not establish a performance distribution. The objective
arbitration fixture pauses one sample deliberately; exclude that setup from any
future normal-play latency certification. Do not run builds alongside runtime
probes: engine startup's platform validation also needs the UBT mutex.
The network runners accept -PackageRoot for cooked clients/authority. Use
RunResetCheck.ps1 for ten world reloads, RunSoakCheck.ps1 for the ten-minute small
slice measurement, and VerifyCleanCheckout.ps1 on a committed tree for a fresh local
clone/build/asset/rule check. Do not run builds or other game tests during the
certified soak. Package manifests retain source and payload hashes. File-backed
cook output avoids the observed Zen staging failure while retaining IoStore/pak.
Keep a runnable checkpoint.
Validate does not write packages; Regenerate is explicit. Hand-authored assets
belong outside /Game/Lockdown/Generated/. The web baseline stays unmodified at
74d3d3e884bf4d8130e1596148b45e50a409039c.

The owner's EngineAssociation=5.8 and normalized .vsconfig are committed in
77305e0. The generated config was corrected to the tested MSVC14.44/SDK26100/
FrameworkSDK4.8 prerequisites, removing the earlier banned 14.50 recommendation.
Ignored Config/Local/Environment.json contains this machine's engine path.
