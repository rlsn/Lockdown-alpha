# B34 combat and life checkpoint

Native combat now shares the B33 authority and private observation boundary.
This is the small uncooked migration slice; courier, Seal and Heart remain B35.

## Combat

- Left click commits an aim direction. The authority selects the nearest legal
  visible body or ward at impact, with an opaque private identity tie-break.
  Damage is 20 after 150 ms, with a 750 ms start cadence, 96 cm reach and an
  80 degree cone. Turning afterward does not redirect that commitment.
- Q requires a quiet, unexposed source and an unalert target's rear. The locked
  attempt warns its target immediately without an attacker reference or position.
  At 500 ms it rechecks life, protection, rear, reach, terrain, sight, sprint and
  target damage history. A successful impact deals 60, followed by 250 ms recovery.
  The cooldown is 18 seconds. Earlier damage or dash interrupts without refund.
- Space applies a server-owned Character Movement constant-force root-motion
  source: 360 cm over 200 ms, seven-second cooldown, no invulnerability. Capsule
  sweeps, walkable slopes and ledge checks remain active. The client waits for
  authority and reconciles through the supported movement path.
- One authority sample gathers every valid due hit before applying any damage,
  then finalizes deaths. A due counterhit still lands; an earlier hit cancels a
  future windup. A strike hits one nearest target, including a 20-HP ward.
- A bounded input queue applies dash, assassination, then basic priority per
  connection/sample. RPC arguments are finite planar unit directions, match/life
  generations and increasing request IDs. A burst-limited controller caches 32
  receipts. A lower-priority action consumes no cooldown when superseded.

The baseline's directional nearest-target attacks need no client-selected target
or canonical actor pointer. Selection and assassination lock stay on authority;
current permitted sight and melee legality are rechecked there. Future selected
interactions use B33's per-connection observed-ID resolver. No lag rewind is added.

## Life and privacy

Contenders have 100 HP, residents 40. Death snapshots the retained level/time delay
and ceiling gold loss once. It unpossesses and destroys the old body, clears ward
charges, and sends only the owner the loss and return deadline. Existing wards
continue until expiry or destruction. Waiting uses a local camera with the same
fog material and only owned ward/public sources.

At the deadline, a connected contender receives a fresh pawn, life generation and
observation identity, full HP and four seconds of protection. The server selects
from legal shared spawn candidates, preferring positions at least 440 cm from
living contenders and otherwise the farthest candidates. Protection blocks both
outgoing actions and incoming damage. Residents return after 20 seconds; their
deaths award no gold or XP. Corpses are separate, nonreplicated 20-second actors
and are never economic or melee targets. No combat death ends the demo.

Exact HP, money, cooldowns, warnings, death receipts and return deadlines remain
owner-only. A revision guard prevents an older replicated state from overwriting
a newer reliable private receipt. Authorized proxy records add only permitted
action/exposure cues; the schema still has no role, private statistics or actor
references. Seven humanoid pieces plus a shared status marker are used for every
role: raised arms for commitments, orange danger, pale protection and amber exposure.
Effects disappear with the proxy. No spatial combat audio or independent trail
actors are introduced. The bounded full-view cap is now 64, including corpses.

## Scheduling and checks

Character Movement runs first. Combat samples current authority poses at 20 Hz;
vision has an explicit combat prerequisite and rebuilds afterward. Due deadlines
resolve at the first sample at/after the deadline. Hitches are logged and never
replayed against a single pose. This is native sampling, not web lockstep parity.

Run `RunTests.ps1`, `RunCombatCheck.ps1` and `RunCombatNetworkCheck.ps1` from Scripts.
The world probe combines controlled deadline evaluations with actual root-motion
physics. The network probe uses a separate authority and two clients, actual
controller RPCs, a natural 20-second death wait, restricted actor registries,
private warnings and post-return combat. Diagnostic poses and resets are gated by
non-shipping command-line flags; normal gameplay has no remote fixture commands.

The network runner accepts `-RoundTripMs 100` or `150`, with `-JitterMs 25` for a
uniform +/-25 ms outgoing delay on each endpoint, using Unreal packet simulation.
Echo RTT and action acknowledgement latency are measured separately. Six scripted
action receipts per client are functional evidence, not a statistical performance
certification. `-Rendered -RenderRole A` opens the 720p client for live review;
`-RenderRole B` opens the 1080p client. The other client and authority use NullRHI.
See TestReport.md for executed evidence and remaining gates.
