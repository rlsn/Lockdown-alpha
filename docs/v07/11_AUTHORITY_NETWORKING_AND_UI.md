# Authority, networking and native UI

## Reuse one integration boundary

Keep actual pinned Unreal engine, native CharacterMovement/path following, Enhanced Input/UMG infrastructure, chosen ability framework and item transaction queue. This package specifies project behavior, not an unverified engine API recipe. Resolve exact APIs against the installed toolchain when implementing. No second damage/MP/cooldown system, separate item inventory, or client-only combat outcome. Store persistent attributes/mastery/cooldowns/quotas on contender state and life-bound effects on the actual body/life.

Versioned records: AbilityDefinition, ItemDefinition, EffectDefinition, BuildState, EquipmentBinding, CooldownFamily, EffectInstance(source receipt/life/expiry), WorldSpawnBudget, RouteActivation/Lease, HeartAttempt and terminal receipt. IDs are stable/opaque at authority; expose only scoped handles needed by each authorized view. A definition ID is public catalog metadata; owning/learning it is private.

All commands validate owner/match/life/revisions, legality and target authorization on server. One request receipt makes duplicates idempotent. Input arm does not cast; release/hold flows through accepted action state. Item use/consume, MP cost, cooldown and source spawn commit together. Failed staging/asset allocation changes nothing. Spell landing/blocking does not refund a successful release. Native movement is not reimplemented with transform teleports for walking/dashing/pushes; explicit blink/swap/gates remain validated teleport semantics.

## Native UX

- Own lower HUD always shows HP/MP, five skills, three quick items, cooldowns, costs and blocked-action reason, regardless of inspected subject.
- Selecting another visible unit changes inspector only. Opponent exact HP/MP/build remain Unknown unless a separate explicit future reveal rule authorizes them. Show only observed cast/status cues, not a full private character sheet.
- Inventory shows24-instance count, weight/capacity, six binding cells referencing the same IDs, two-hand reservation, quality/tier, effective-vs-natural attributes, clipped gear bonuses and bind/unbind hold states.
- Learning panel separates known, trained rank, effective potency, equipped, unavailable natural gate and Circle. Preview full draft spend; station hold commits once. High-circle known but ineligible remains clearly labelled.
- Item tooltip identifies reusable vs consumed, passive binding requirement, quick reference, family cooldown and drop/retain status. No Use button can bypass quick slots in combat.
- Workshop shows recipe graph, exact owned inputs, missing input buy cost, scarce supplied catalyst, output Standard quality, fees, time, final count/weight and cooldown inheritance. Disclose loss of component craftsmanship before commit.
- Objective panel shows four independent routes, own progress, PUBLIC Charter meaning, static warning/window time, private lease remaining, tower count, visible objective cargo risk and why a claim is blocked/cancelled. No hidden global key-holder tracker.

Right-click terrain moves; LMB selects; A arms attack; skillQ/W/E/R/D; items1/2/3; F context hold; T peer invitation. All mapping-derived, remappable with conflict checks; old quick4 not active. Existing modal/drag/targeting/minimap/world input priorities and stop/focus cancellation remain. Panel opening does not pause others, and a click never also issues a world order. Reading book from inventory is a quiet institutional-style learned action, not an extra combat item slot. No branching dialogue or player chat required.

## Observation and privacy

Serialize filtered views, never a full enemy actor sheet hidden by widgets. Apply same boundary to cursor/hit testing, outlines, minimap, target previews, audio/VFX and logs delivered to clients. Spell impact feedback must not leak unseen hit targets. Public cast/Heart messages are deliberate exceptions only for their named facts. Own combat log explains cause and response category without sending opponent unobserved library, mana balance or identity. Ledger Mark/Seer's effects authorize only specified geometric cues. No class prefix, actor class, rejection code or different trade schema identifies human versus NPC.

Peer trade retains exact-revision/digest double consent and atomic transfer. Rebinding/equipment crafting/outgoing combat cancels trade before executing. Incoming wallet changes invalidate approvals; offered gold remains stealable. Only voluntarily offered items/gold are visible. Equipped gear needs prior quiet unbinding before offer. Objective keys/artifacts/relics may be offered only if ordinary physical-transfer rules allow; transfer revokes incompatible access and retains instance cooldowns in same commit.

## Same-batch ordering and lifecycle

Adopt the full order in victory specification; do not implement separate Heart completion callbacks that race damage, death or window closure. Native movement → due combat/expiries → simultaneous death/drop → scheduled institution updates and noncombat transactions → due Heart completion → deadline. Due events are ordered by scheduled server time, not render frame. Use stable role-neutral ordinals on exact transaction ties. Terminal result prevents subsequent commits and releases reservations.

Every temporary collision/sight source needs expiry, destroy, owner death, match end and reset cleanup. Source recreation cannot replenish cache/market/reward budgets. Projectile references carry original victim life and fail on respawn. A late client ack is reconciled against receipt instead of repeating use. Family cooldown endpoint travels with reusable item and derived recipe output; max of all consumed input/family endpoints plus new owner's family state, never a reset.

## Required technical evidence

Cold build/cook/package on actual environment, real saved native assets, server plus at least two independent restricted clients, measured100/150 ms RTT with up to25 ms one-way jitter, relevant focus/remapping/ramp/nav/smoke/ability/claim cases, and payload privacy. Reuse valid unchanged predecessor evidence; rerun changes rather than blindly all history. Test1280×720,1920×1080,150 percent OS scale and125 percent text. Measure baseline frame/view/transaction costs and new AoE/anchor query costs against actual existing budgets, record hardware/settings. A Python model or local listen-server alone does not certify remote authority or human usability.
