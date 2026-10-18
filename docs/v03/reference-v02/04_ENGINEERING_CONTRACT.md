# v0 2 engineering contract

## Architecture and migration boundary

Keep the authoritative TypeScript simulation separate from the browser renderer and transport. Reuse Phaser, Vite and Colyseus where present. Audit actual dependency compatibility and retain exact versions and the lockfile unless a necessary change is documented. This release requires no new external service or database.

Use one fixed 20 Hz simulation with 50 ms ticks and 10 Hz filtered snapshots. Convert durations to integer ticks with ceiling. Store absolute endpoints for cooldowns, return timers, temporary buffs, ward expiry and channel completion. Seeded placement, AI, offers, events and transaction tie resolution use separate RNG streams. Replay claims must name the configuration and ordered commands; no cross-runtime bitwise determinism is promised.

Introduce domain modules for life cycles, objectives, profession, inventory, vision sources and AI observations. Existing movement, collision and combat can remain if their assumptions are corrected. Do not serialize canonical room state wholesale: per-client public, private and visible data must be built deliberately.

## Persistent contender and temporary body

| State | Ownership and required data |
| --- | --- |
| Contender | Stable server identity, guest owner or AI policy, level, XP, gold, upgrades, profession, counters, cooldowns and current life status |
| Life body | New opaque ID per spawn, contender reference kept server-only, pose, HP, protection endpoint, action, damage history and alive state |
| Respawn entry | Unique death ID, contender ID, death tick, level snapshot, fixed return tick and applied loss receipt |
| Resident | Routine identity, current body and route, death and return ticks; no profession rewards or winner eligibility |
| Camp | Two life records, centre, leash, reset clock, cleared generation and full-return tick |
| Tower | Public ID, HP, shield, sensor, retaliation memory and one destruction receipt |
| Contract | Private ID, owner, destination, stage, channel token, completion receipt and next acceptance tick |
| Seal | Item ID, creator, exactly one ownership state and optional dropped point; separate from body and corpse |
| Ward | ID, stable owner contender, point, height, HP and expiry tick |
| Vision | Per-source geometry, current masks, per-contender exploration and derived visible entities |
| Heart | Ordinary access, held attempt records, current progress, sensor expiry and one terminal claim receipt |

Life status is ACTIVE, WAITING_RESPAWN or WITHDRAWN. A dead body is not the contender identity. A dead contender is still eligible to return unless withdrawn or the match ended. Neither remaining-body count nor contender death can independently end a match.

Keep corpses as short-lived presentation records. A corpse ID cannot be used as a second economic target. Dropped Seals remain independent after corpse expiry. Remove old body references from target locks, input caches, pending damage and interpolation when a life ends.

## Tick order

1. Expire reconnect grace and process withdrawals. Apply a withdrawal's drop and cancellation once. Process due respawns, ward and corpse expiry, camp returns and market events. At 480 seconds set Showdown for future death calculations.
2. Validate commands, match and life generation, input sequence, actor ownership, rate limits and action priority. AI produces commands from its previous authorized observations. Neutralize input older than 250 ms.
3. Apply movement, facing and protection expiry. Use shared collision including cliff and ramp rules. Compute the sight needed for action and target legality.
4. Resolve due combat, tower and camp impacts and recovery using simultaneous damage rules. Damage on a channel's completion tick cancels it. Finalize deaths, loss receipts, credit, camp clear bonuses and drops. A body protected at the damage evaluation point takes no damage.
5. Apply objective destruction, rewards and new access facts. A central tower protected in the combat evaluation cannot take damage from an impact on the same tick that an outer tower falls; it becomes attackable for later ticks. Resolve purchases, work completions, crafting, Seal pickups and ward placement only for still eligible living bodies.
6. Evaluate held Heart attempts after movement and deaths. Contesting presence resets progress. A claim's progress is `currentTick - uninterruptedStartTick`; a newly started attempt is zero, not one tick. Apply the single terminal claim transaction if ten seconds are complete and all conditions still hold. Only after this evaluate the 720 second no-claim deadline.
7. Recompute public sources and per-contender visibility after destruction, death, pickup and placement. Update explored masks. Emit filtered snapshots, immediate authorized warnings, receipts and logs. Never send a position using a tower that was destroyed earlier in this tick.

Use this same order in tests and bot-only simulation. A lost client cannot continue a held channel using its last E state. A freshly returned protected actor cannot contest a claim. No later callback may overwrite an already committed match result.

## Action and channel contracts

Each request includes match ID, current body ID where applicable, increasing input sequence or request ID, and a bounded payload. The connection determines ownership; it cannot supply another contender ID to control. Refresh movement and held E through input frames. Keep explicit `beginContext`, `cancelContext`, `buy`, `acceptContract`, `craftSeal`, `pickupSeal` and `placeWard` semantics internally even if one E key drives them in the UI.

Priority on a tick is dash, assassination, basic attack, medkit, ward placement and contextual interaction. An otherwise legal combat action can cancel a work, craft, medkit or Heart channel; failed action requests do not consume or cancel a channel. An otherwise legal medkit use may cancel work, crafting or a Heart channel; ward placement and a second contextual action reject while a channel is occupied. Upgrade selection is independent but allowed only while alive and unshielded. An occupied attack or dash blocks a new civic or item action. Dead, withdrawn and protected states reject gameplay actions as specified.

Channels store owner, current body ID, target POI, type, start tick and transaction ID. Recheck range, stationary input, sight where required, life and connection every tick. Work, crafting and Heart channels additionally require refreshed held E; a medkit started with R does not require holding E or R. At the Guild, selecting Craft in the panel arms that interaction and displays Hold E to craft. An interrupted channel resets or cancels as specified; it cannot finish through an old timer callback. Client animations and progress bars never grant resources or victory.

Use v0.1 limits as the starting transport guard: maximum 4 KiB payload, 40 input messages per second with burst 20, ten non-input requests per second with burst ten, one accepted gameplay action per tick and at most 4096 retained action IDs per client per match. Reject beyond the bound instead of evicting deduplication state. Persistent flooding is handled as in the baseline. Replay acknowledgements may repeat, but effects may not.

## Atomic transactions and invariants

- Death ID applies HP termination, gold loss, item loss, Seal drop and one return schedule at most once.
- Tower ID and generation permit one destruction and one reward. Central damage immune status is an authoritative fact.
- Contract ID permits one delivery reward. Death or cancellation cannot complete an old contract afterward.
- Craft ID revalidates rank three, one-craft counter, free carry slot and 150 gold at commit; all deductions and Seal creation happen together or not at all.
- Seal ID has one owner or one dropped position, never both. Claims, death, withdrawal and pickups cannot duplicate it. Pickup contention uses a seeded shuffled request order independent of role.
- Purchase ID checks balance, stock rules, life, caps and inventory in one commit. Receiving a reward does not bypass purchase caps.
- Heart has one terminal receipt. A result requires a valid ten second claim and cannot be created by an elimination callback.

Assert these invariants in the headless simulation and serialization tests. Use integer percentage arithmetic for gold loss to avoid floating point rounding at exact values. Track repeat-victim rewards by persistent contender IDs, not fresh body IDs; respawn must not bypass that window. Do not implement speculative client purchases that become authoritative before acknowledgement. Crafting may be interrupted without cost; medkit interruption consumes the accepted item. Those are deliberately different transaction policies.

## Vision and replication

Compute shared tower masks once per tick or once per visibility revision, then combine them with each owner's body and wards. Cache static ray geometry only if invalidation is correct. Do not retain a destroyed source for interpolation convenience. At most two placed wards per contender bounds private source count. Map dimensions produce 60 by 40 fog cells; send a compact current mask and explored changes with periodic full recovery snapshots.

Visible entity inclusion requires an authorized source and exact visibility, not only a camera rectangle. Source union must work across distant ward and tower regions. The client can know static map data without receiving dynamic state there. Do not put owner mappings, bot flags, hidden camp contents, contract destinations or exact Seal carrier positions into shared room metadata.

Use stable public POI IDs and opaque body and item IDs without role prefixes. A bot's observation builder should reuse the same filtered data contract as a player. It must not reach canonical state to choose a hidden target. Server mechanics such as spawn safety and global tower status may use canonical data only as their explicit rules permit.

### Snapshot additions

| Block | New fields and restrictions |
| --- | --- |
| Public match | Ruleset, elapsed tick, Showdown status, deadline, four public tower summaries, ordinary Heart lock and authorized claim progress |
| Self | Life state, body ID if active, fixed return tick, gold loss receipt, profession rank, commissions, contract stage and destination, craft count, Seal ownership, ward charges and own sources |
| Vision | Current mask, explored state or changes, authorized source summaries; no enemy source owner mapping |
| Visible world | Visible bodies, corpses, wards, camp creatures and dropped Seals; no hidden cached positions |
| Coarse objective events | Seal crafted, dropped or changed district; district only, no exact carrier ID or coordinates |
| Results | Winner, claim route, duration, no-claim reason if applicable and final participant summaries |

Dead clients receive only owned ward and public sight, not another contender's full snapshot. The active body is omitted while waiting. Public tower health and broad objective events are intentional exceptions to local sight and must be tested as such. Target assassination warnings remain immediate even when the attacker is hidden; the warning does not include hidden attacker coordinates.

Version the protocol for this mode. A v0.1 client must not interpret a respawn-waiting state as a final elimination or a Heart draw as survivor placement. Reject incompatible rulesets on join with a clear message. Reconnect sends a fresh full authorized view; old body input and prior-life context requests fail even within the same match.

## Collision and prediction

Share static wall, tree, tower-base, ramp and cliff data between client and server. Retain movement substeps of at most seven pixels to prevent dash tunnelling. A ramp changes height only along its navigable cells. Validate every pad, job station, camp and public approach through path tests.

Predict local movement only; reconcile using acknowledged input. Do not predict HP, respawn ownership, gold, Seal pickup, crafting or capture completion as authoritative. Interpolate visible remote bodies, removing them immediately when authorization ends. Do not delay assassination danger warnings behind interpolation. Keep the baseline current-state melee validation without unreviewed lag rewind.

## World reset and operational delivery

Rematch clears contender progression, every life, ward, explored mask, corpse, contract, camp buff, Core, Seal, tower receipt, RNG state and claim attempt. Create a fresh match ID. Keep room and host state only where intended. Cross-room information, timers and objective events must remain isolated.

Guest sessions, allowed origins, local setup, health checks and vulnerable reconnect behavior remain as in v0.1. A server restart loses rooms. Do not add accounts, storage or public deployment. If all humans disconnect, preserve only the applicable grace windows and then dispose, regardless of AI objectives.

Record structured events for commission acceptance and delivery, craft, pickup, drop, tower damage and destruction, sensor changes, death cost, return, camp clear, market shift and claim begin, interrupt and complete. Include match, body generation, config, tick and private server IDs as appropriate; never export guest secrets to clients or logs. Summarize why AI chose its next goal in a bounded local debug trace without transmitting hidden world state.

Deliver code, lockfile, setup, tests, a baseline audit, migration decisions, configuration reference, verification results and known limits. Include a no-code reviewer demo for both routes, respawn, Seal theft and sight loss. Test harness controls must be local-only and disabled in ordinary play. Fixtures may shorten time for rule tests but the final route observations must identify whether they used production tuning.
