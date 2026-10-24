# B72 — geometry and control

24 October 2026. Based on B71 `88a43a1`. This slice extends the v07 integration
profile; normal frontend and explicit v06 regression retain V05. B73–B77 remain
open. The 93 adopted spec files remain byte-preserved.

## Delivered behavior

The shared authority action pipeline now accepts wire version2 with a bounded
static ground point and an explicit targeting mode. The server checks mode,
match/life, build revision and catalog digest before queueing. Unit targets must
still resolve through the observer's currently authorized handle at acceptance
and release. Ground/cone casts use static map geometry and can aim into fog;
they never resolve a hidden body to validate placement. No invalid-ground response
reports a hidden target or its location. Old wire versions reject.

Smoke sight uses analytic circle-union intervals. A segment wholly inside one
connected union stays clear; entry, exit or crossing an exposed boundary occludes.
Ordinary sight and detectors share smoke, static wall and authored elevation
checks. Detection authorizes current geometry only; concealment remains on its
source. Blind limits own-body sight to200cm and leaves ward/detector sources
independent. Concealed bodies and undetected rival traps are removed from payloads
and fail current observed-handle resolution, including between publications.

Fields are server-only actors with bounded global capacity48 and per-owner/type
limits: ward/trap2, smoke/detector/grounding1. Source life, owning pawn/connection,
HP and absolute expiry govern validity. Allocation/capacity failure before release
spends neither MP nor cooldown. Wards/detectors supply only their owner's geometric
sight. Traps arm after1s, select one eligible body deterministically, root it for
1.6s and consume once. Non-smoke fields can be attacked after legal observation;
ordinary dispel does not delete geometry. Grounding is recomputed from live emitter
geometry and blocks dash while allowing walk and forced movement. Source loss,
leaving geometry and expiry end its authority.

Force Wave uses native capsule sweeps in10cm steps with static floor/slope/step
checks. It stops at walls and unsafe ledges, allows incoming push on rooted bodies,
and cancels held work only when actual planar displacement exceeds native numerical tolerance.
A native submillimeter case distinguishes a real small movement from zero. Native
NPC casts use the same dispatcher; build refresh retains absolute cooldowns for
all delivered skill IDs. Existing
sentinel/boss bodies are immune to control/displacement; official service actors
are not ordinary body targets. No teleport is used for the effect.

Counterpulse carries explicit interrupt plus silence in one hostile targeted
packet. Spell block rejects the whole packet before its components. Control guard
strips one stun/hex component only; damage/silence survive. Remedy's self guard
lasts1.5s and starts an8s major-protection family endpoint retained on the owner
through build refresh/death. Activation breaks concealment. There is no blanket
channel immunity. External Remedy requires explicit current-life aid consent,
rechecked at release. The bag's Items page exposes consent ON/OFF; death clears it.
Purge now removes another body's eligible negative tags only with consent,
otherwise it performs positive dispel. Hex blocks attack/skill/item/service/dash
and permits half-speed walk. Break suppresses only explicitly breakable passive
queries, never ordinary attributes or natural requirements. B73/B75 must connect
future gear passives to that query; no unavailable gear effect is claimed here.

v07 Heart attempts keep their independent timer through ordinary positive damage
and nearby opponents. Silence/mute/root alone preserve a stationary claim. Stun,
hex, explicit interrupt, lethal damage, deliberate self-cancel and actual forced
movement cancel it. Starting a claim ends concealment and clears navigation.
Accepted combat requests explicitly cancel the claim before their action is
validated. The retained Charter access route is unchanged; full four-route access,
chronological stalled ticks and exact completion ties remain B76.

## Content and acquisition

Fourteen implemented new definitions are imported from the adopted catalog. The
current staged file is `Content/Lockdown/Data/V07/combat.json`; `b71_combat.json`
is a historical snapshot. `SyncV07CombatData.py --check` verifies the current
subset and generated native contract checksum. Import fails closed on every
unsupported semantic edit, including unknown operations/payloads. Bootstrap
refreshes only the tactical asset when this digest changes; baseline generated
assets are validated in place. Runtime checks native/staged equality.

| Added ability | Actual effect | Acquisition scope |
| --- | --- | --- |
| Veil Step | 5/5.5/6s concealment | Guild40g book |
| Counterpulse | Explicit interrupt and1.3s silence | Guild40g book |
| Farwatch Ward | 40HP,560cm sight,60/70/80s,limit2 | Guild40g book |
| Wire Snare | 20HP,1s arm,45s lifetime,100cm trigger,1.6s root,limit2 | Guild40g book |
| Smoke Veil | 5s smoke,rank400/450/500cm radius | Guild40g book |
| Seer's Flare | 1HP,5s detector,rank350/375/400cm radius | Guild40g book; natural WIS8 |
| Grounding Field | 6s,350cm emitter,rank90/104/117HP | Effect implemented; CircleII source B74 |
| Force Wave | 500cm/60-degree cone,push250cm | Effect implemented; CircleII source B74 |
| Remedy Bind | Consented external strong dispel or self preguard | Effect implemented; CircleII source B74 |
| Break Sigil | 4s break | Effect implemented; CircleIII stock/source B74 |

CircleII/III entries are deliberately excluded from the Guild40g quote and
purchase path. Diagnostic fixtures grant their learned ranks to exercise effects;
that is not normal acquisition. Remaining14 new abilities and39 new item
catalog definitions remain unavailable until their assigned implementation slices.
Native natural gates preserve OR alternatives and AND clauses, using own natural
attributes at learning and cast/release. Rank overrides apply to their named
field only. Existing INT potency applies only to declared radius/anchor-HP fields;
full equipment/potency progression is B73. No new hex/blind spell is invented;
their contract is tested through status fixtures and native sight/movement rules.

Observed fields append a presentation kind to the existing eight-field tuple.
The record contains no caster, owner, source-life, HP, build or private identity.
Ward-source count is explicitly bounded at8 (legacy wards plus tactical sources).
Public GameState stays at34 fields. Owner-only consent/protection endpoints remain
in the owner state. Visible fields have native named proxies; full rendered cues,
audio, OS scale and human readability remain B77 acceptance.

## Verification and remaining boundaries

Final focused gates pass: pinned Editor build, native asset validation,29 rule
tests,59/35/34 authority/client-A/client-B geometry checks,34/18/15 B71 combat
regression checks and56 V05 authority checks. All three exported economy ledgers
are independently replayed. See `evidence/B72/Summary.json` for exact run paths,
measured transport, scope and manifests.
The geometry fixture uses a native authority and two independent clients, real
RPCs and diagnostic ranks/placement/access. Its small collision wall and injected
status/source cases are labelled fixtures, not a normal journey or authored world
expansion. Final evidence does not certify cold packaging, human input, long
sessions, ten restarts, all races, full150ms matrix or B76 route/timing semantics.
Inherited V06-D01–D08 remain open. Use the working FeatureLedger for those gaps.
