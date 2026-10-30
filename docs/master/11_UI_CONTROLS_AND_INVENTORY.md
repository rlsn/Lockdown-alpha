# Controls, UI and interaction design

## Persistent control and clear ownership

The interface combines RPG build detail with tactical spatial awareness. The player always controls their own body. Selection changes the inspector subject; it never transfers commands to a selected NPC. Own HP/MP, prepared actions, accessory bindings and relevant status remain available while inspecting something else.

| Intent | Default input | Behavior |
| --- | --- | --- |
| Select / service | Left click | Inspect; open observed storage or trader, approaching first if needed |
| Move / ordinary interaction | Right click | Move to ground; approach and pick up an ordinary item, or open/close a door once |
| Basic attack | A | Explicit attack intent; neutral right-click does not attack |
| Shared actions | Q / W / E / R / D / G | Six prepared learned/profession/body-equipment actions |
| Accessories | 1 / 2 / 3 / 4 | Exact bound physical accessory actions |
| Eligible POI | Stand at the POI | Begin its eligible behavior once on entry; no interaction key |
| Peer trade | Inspector button | Offer to an observed nearby humanoid; explicit transaction review |
| Stop/cancel | S / contextual cancel | Stop movement or the current cancellable intent |

Bindings are remappable. Labels follow the current mapping and collisions must be visible. Retained preferences migrate without silently overwriting customized keys. Navigation reports arrived/blocked/stopped truthfully and stops stale hidden-target pursuit.

## Screen hierarchy

The world carries motion, position, cover and effect telegraphs. The minimap provides public static geography, explored substrate, authorized local observations and explicit public objective notices. The lower inspector provides selected-subject context. The own command bar remains stable and displays actual MP, cooldown and binding/status reasons.

Private opponent class, attributes, ranks, knowledge, loadout, HP/MP, purse and human ownership remain Unknown unless a specific mechanic authorizes a fact. Hover, outlines, target handles and audio cannot disclose what the inspector withholds. Camera pan and map routing neither reveal space nor grant remote service reach.

Inventory and development panels expose detail on demand. Service panels reuse the shell for supply, markets, bank, work, workshop and acquisition. Dialogue trees are not required to discover a reliable transaction.

## Action and interaction feedback

An action should communicate source, target mode, range, cost, cooldown, commit point and why it is blocked. Distinguish learned action, body-equipment active, accessory, passive and empty slot. A tooltip must not advertise a verb that has no server operation.

A source action shows item/ability name, available verb, ownership restriction and channel preview only when authorized. There is no generic Interact key or object-specific shortcut. Placement shows intended footprint/endpoints and gives generic invalid feedback without revealing a hidden body. Carrying shows the prop, reduced speed, suppressed actions and Place/Drop.

Press or click once to issue a channel. Releasing the input leaves it running. S stops the current action; movement, focus loss, loss of eligibility and lifecycle changes follow the action’s interruption rules. Progress comes from authoritative start/end times. A stopped POI waits until the player leaves and re-enters; focus restoration never replays cancelled input. Network heartbeats belong to the active order, independent of physical key state.

Brief client stalls should not masquerade as a deliberate stop. The current
implementation permits a heartbeat gap shorter than two seconds, then cancels
and frees its reservations; a late refresh never recreates an order. This is
connection tolerance, not a delay on S or gameplay interruption. Feedback should
identify whether movement, damage, eligibility or lost command liveness stopped
the action. The [rendered regression](../v09/B91_LIVENESS.md) checks both gap
survival and timeout without claiming human usability or performance acceptance.

Automatic POIs wait for a stationary character with no movement order pending
or following. Crossing a service radius must not take over a movement command.
Track leaving/re-entering even during movement so stopping an action does not
make it restart just because the character takes a small step inside the POI.

## Inventory and transactions

The bag holds at most 24 unique instances and a weight budget. Equipped items are the same instances, referenced by eleven bindings. Accessory actions require a physical binding; body-equipment actives additionally require a shared action slot. Opening a bag is immediate; changing bindings takes quiet plus commitment.

Tooltips distinguish natural/effective attributes, tier/quality, known/trained/prepared/effective rank, unmet gates, clipped bonuses, cooldown family and death behavior. Craft previews list exact ingredients, fee, Standard output, capacity and quality loss before committing.

Peer trade uses versioned offers, shared reservations, Ready, final review and separate confirmations from both players. Offered gold remains exposed to ordinary loss until commit. Death releases locks before drops. Only voluntarily offered terms are revealed. Items must be legally unbound before transfer.

World storage uses the same physical instances as the bag. A common cache is a
finite initial endowment; an empty barrel contains no implied oil or reward.
Opening a visible container grants a local, revision-bound contents view.
Taking, storing and breaking revalidate that view and the exact item before
ownership changes. A rival who arrives first can take the item; a retry cannot
take a second copy. Contents close when the body leaves reach, loses sight or
changes life. A map marker, camera cutaway or remembered container never grants
access to its live inventory.

Heavy cargo creates a tactical commitment. The prop remains in the world with
one carrier, consumes neither a bag slot nor a new equipment slot, suppresses
hand actions and services, and reduces movement to 70%. Its contents add weight.
Placement must preserve doorways, stairs, public routes and other bodies. Failed
placement retains the cargo; damage, death or an explicit ability request first
requires a safe drop. Dropping a prop cannot displace another character or turn
an illegal action into a legal one.

Interaction feedback should make those commitments easy to judge: current
container, selected physical item, channel duration, required quiet time, cargo
state and a clear reason for refusal. The interface must support deliberate selection among nearby
objects, readable contents, visible placement footprints and the same input
cancellation rules as combat.

Portable tools retain their physical fuel through binding, storage, transfer,
drop and life changes. A lit tool keeps burning until extinguished or empty;
changing a reference cannot refill it. Active use requires the real accessory
binding and item permission, including Mute. Cosmetic light can communicate a
visible tool without becoming a sight source or exposing a hidden carrier's
inventory. Public presentation carries a light cue; precise fuel belongs to the
owner or a legally disclosed transaction view.

Ignition feedback should separate available fuel, elapsed light fuel, the
channel's explicit cost and its family cooldown. Show the target, short channel,
reserved item state and cancellation reason before clearance commits. A burning
sapling still blocks movement until it falls; make that delay readable. A
flame cue must not promise a harvest reward or imply that every nearby tree
will burn. Activate the bound Torch, then confirm a target once. Non-color cues and target readability still need user testing.

Distinguish preparing a portable route from using one. A Rope requires its real
accessory binding to attach; once installed, traversal is a single-confirmation world action
available to rivals. The original item leaves the bag and returns unbound when
its owner detaches it. A Ladder stays world cargo throughout. Show the approved
pair, landing, channel progress and refusal reason without exposing the hidden
user or deployer's private identity. Control, damage, stop or life change
must resolve to a supported landing before ordinary input resumes.

Fixed storage should read differently from movable cargo. An empty piece of
furniture can accept property without promising free stock or a destruction
reward. Inspection grants the same current, local contents view as other
storage; take/store feedback identifies the selected physical instance and
quiet-time commitment. Its shape can block a route without granting concealment.

## Aimed consumables and terrain feedback

Preparing a flask binds a real physical instance to an accessory. Aiming shows
where it can legally release, its range, the selected supporting floor and the
area affected. Confirmation commits the inherited windup; interruption, invalid
terrain or exhausted field capacity must leave the item and family cooldown
unchanged. After success the item disappears once and the family timer remains
visible through any later binding change.

The result needs a readable shape and state, including Wet, Oil and ice. Color
alone is insufficient for the final interface; add icons, edge treatment or
material motion alongside a short state label. Oil should communicate preparation
for a later reaction, without promising automatic ignition. Ice communicates
slower grounded movement, without suggesting a direct-hit Stun.

The B91 bag selects exact instances; a binding button issues the2s preparation order once. Keys1–4 activate bound items. Confirm a flask target once through the ordinary targeting path. A complete footprint preview, enlarged text and human readability still require product UI work.

## Input arbitration and accessibility

Commodity trading uses LMB on the physical counter and one click on a priced
Buy/Sell row for one unit. Show local stock and the owner's wallet beside the
terms. A refusal should identify what changed: access, quote, inventory, funds
or capacity. Refreshing a quote never confirms a purchase on the player's behalf.
Close stale sessions when their body leaves or becomes ineligible; preserve the
receipt for an already completed transaction. The [B92 counter review](../v09/B92_SERVICES.md)
checks native remote requests and a 720p layout, while enlarged text and human
usability remain open.

Text entry, modal, drag, targeting, panel, minimap and world each own input explicitly. A click cannot also pass through to a world command. UI does not pause opponents. Distinguish selection, attack and interaction visually and use text/icon/timing cues alongside color.

Verify practical tasks at 720p, 1080p, enlarged text and OS scaling: find an action reason, remap, navigate, learn/prepare, equip, compare, buy, trade, cancel and recover after death. Observe real users and independent clients. Screenshots establish layout only; native input and restricted payloads establish behavior.

## Source discipline in world design

Do not add an interaction key when adding a tree, bottle, shrine or door. First
choose whether the behavior is an ordinary pointer interaction, automatic POI,
basic attack, or an active from a prepared ability/physical item. A reusable
free “interact ability” is not a valid substitute for a source. Binding and
six-action limits continue to apply. Specialized extraction and lock work need
authored sources; ordinary pickup and doors remain convenient.

The approved B91 Forestry Knife makes this concrete: target a living sapling
with a real bound knife, select Harvest or Chop, confirm once and see the4s
channel. Harvest yields one finite Fiber; Chop yields none. Neither F nor Z
exists as a tree command. Binding, storing or transferring the same knife does
not multiply it. See the [current control decision](../v09/CONTROL_POLICY_OVERRIDE.md).

An issued source order should preserve the player's intention while approaching.
For a visible target outside item reach, confirm once, walk into legal range,
then begin the channel and its progress display. Reserve or spend at the proper
authoritative start/commit, rather than charging for the walk. S, a new movement
order or lost eligibility can end the approach. The target's current observed
handle, item binding and eligibility must be rechecked on arrival.
