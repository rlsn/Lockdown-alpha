# Controls, UI and interaction design

## Persistent control and clear ownership

The interface combines RPG build detail with tactical spatial awareness. The player always controls their own body. Selection changes the inspector subject; it never transfers commands to a selected NPC. Own HP/MP, prepared actions, accessory bindings and relevant status remain available while inspecting something else.

| Intent | Default input | Behavior |
| --- | --- | --- |
| Select | Left click | Inspect an authorized body/object or static location |
| Move/approach | Right click | Native navigation to ground or a currently observed target |
| Basic attack | A | Explicit attack intent; neutral right-click does not attack |
| Shared actions | Q / W / E / R / D / G | Six prepared learned/profession/body-equipment actions |
| Accessories | 1 / 2 / 3 / 4 | Exact bound physical accessory actions |
| Context | Hold F | Available legal world/service interaction |
| Peer trade | T | Offer to an observed nearby humanoid |
| Stop/cancel | S / contextual cancel | Stop movement or the current cancellable intent |

Bindings are remappable. Labels follow the current mapping and collisions must be visible. Retained preferences migrate without silently overwriting customized keys. Navigation reports arrived/blocked/stopped truthfully and stops stale hidden-target pursuit.

## Screen hierarchy

The world carries motion, position, cover and effect telegraphs. The minimap provides public static geography, explored substrate, authorized local observations and explicit public objective notices. The lower inspector provides selected-subject context. The own command bar remains stable and displays actual MP, cooldown and binding/status reasons.

Private opponent class, attributes, ranks, knowledge, loadout, HP/MP, purse and human ownership remain Unknown unless a specific mechanic authorizes a fact. Hover, outlines, target handles and audio cannot disclose what the inspector withholds. Camera pan and map routing neither reveal space nor grant remote service reach.

Inventory and development panels expose detail on demand. Service panels reuse the shell for supply, markets, bank, work, workshop and acquisition. Dialogue trees are not required to discover a reliable transaction.

## Action and interaction feedback

An action should communicate source, target mode, range, cost, cooldown, commit point and why it is blocked. Distinguish learned action, body-equipment active, accessory, passive and empty slot. A tooltip must not advertise a verb that has no server operation.

A context choice shows object name, available verb, ownership restriction and hold preview only when authorized. Crowded F selection cannot steal a combat click. Placement shows intended footprint/endpoints and gives generic invalid feedback without revealing a hidden body. Carrying shows the prop, reduced speed, suppressed actions and Place/Drop.

Release of a hold, focus loss, moving out of range and lifecycle changes cancel through the same intent authority. Cancelled input is never replayed when focus returns.

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
container, selected physical item, hold duration, required quiet time, cargo
state and a clear reason for refusal. Prototype keyboard shortcuts are delivery
tools. The long-term interface must support deliberate selection among nearby
objects, readable contents, visible placement footprints and the same input
cancellation rules as combat.

## Input arbitration and accessibility

Text entry, modal, drag, targeting, panel, minimap and world each own input explicitly. A click cannot also pass through to a world command. UI does not pause opponents. Distinguish selection, attack and interaction visually and use text/icon/timing cues alongside color.

Verify practical tasks at 720p, 1080p, enlarged text and OS scaling: find an action reason, remap, navigate, learn/prepare, equip, compare, buy, trade, cancel and recover after death. Observe real users and independent clients. Screenshots establish layout only; native input and restricted payloads establish behavior.
