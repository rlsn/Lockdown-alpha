# HUD selection and minimap

## One stable hierarchy

Use a bottom band with minimap on the left, an inspector beside it, and the controlled character's command strip across the remaining width. The inspector changes subject; commands do not. A small always-visible self HP/life strip stays above the own hotbar. Top centre shows match time and objective state; a small task card shows the active job or chosen goal. Avoid parallel old and new HUDs.

| Surface | Required content |
| --- | --- |
| Own status | Current/max HP, XP/level, gold, protection/respawn, unspent points indicator |
| Own command strip | Four equipped skill slots Q/W/E/R, four quick-item references 1–4, action/channel progress |
| Inspector, self | Six attributes, derived values, own class/subclass and links to inventory/development |
| Inspector, other | Authorized observed subject, selection silhouette, public cues and explicit Unknown fields |
| Minimap | Fog states, authorized dynamic markers, static services, sources and camera footprint |
| Objective | Deadline, Seal possession, claim state and clear acquisition/no-acquisition result |
| Task card | Current courier stage, destination and relevant work credits; concise cancellation |
| Feedback | Target mode, progress, rejection and idempotent result acknowledgement |

Show only resources backed by rules. No MP bar is drawn in the default no-mana profile. Resource layout can support a real future MP entry without reserving a misleading empty bar. Never display opponent HP percentages merely because a generic unit panel has an HP slot.

## Selection is observation

Left-click an observed unit to select its neutral observed handle. Clicking self portrait or terrain restores the own inspector. Selecting does not stop a move order, rotate the controlled body, change hotbar ownership or open a major panel. Drag-box and multiple selection are deferred.

For an observed non-owned unit, show an observed silhouette/label, currently authorized public effects and available neutral interaction verbs. Its exact HP/maxHP, level, attributes, class/subclass, learned/equipped skills, ranks, gold and inventory remain Unknown in the default profile. Do not request those private fields and merely hide them visually. Use the same field schema and unknown labels for residents, trained NPCs and contenders. Explicitly hostile sentinel cues remain public because that encounter is visibly hostile; they do not permit revealing arbitrary private data.

Authorized future reveal effects must name the field, recipient, source and expiry. None is added by v0.5. A currently witnessed skill effect can identify that skill in transient feedback, not reveal the remaining bar. Do not invent permanent knowledge journals or remembered enemy cooldowns in this sprint.

When the selected observed unit leaves the current authorized view, invalidate its handle and outline, clear all dynamic values, cancel target approach/armed target and reset the inspector to self with a brief Target no longer visible message. No last-known moving marker, hidden reference, attached effect, tooltip or pointer hitbox remains. Reappearance requires explicit selection; no automatic reacquisition by private identity.

Owner state can remain visible while the camera is elsewhere. A unit observed through a public tower or owned ward can be inspected under the same mask, but spells, theft, trade and contextual actions still require own-body geometry and range. Show Out of reach rather than silently navigating or using source sight as action sight.

## Slot and tooltip states

Each own skill slot has name/icon, mapping label, rank, pending/ready/targeting/casting/channel/cooldown/blocked state and a tooltip. The tooltip states outcome, own scaling, range, cast duration and cooldown from current definitions. Missing or unequipped knowledge appears only in the learned-library/development view, not as an extra active hotkey.

Quick items have a distinct border and Items label. Book icons say Learn, never Cast. A ward is a finite charge; a Seal occupies inventory and objective status but has no reusable quick-use action. Pending requests cannot optimistically grant an item, reset a cooldown or update a private balance as final.

Use one channel widget for work, reading, Patch, checks, preparation, fabrication and Heart with different metadata. Show action, required hold input, server-confirmed start and projected finish; corrections stay visible. Pending local progress is visually distinct. A result appears once per receipt even if its acknowledgement repeats. Essential ambush/cast warning remains visible above panels without exposing a hidden origin.

## Minimap information contract

Render unexplored, explored-unseen and currently visible terrain distinctly, with shape/texture alternatives to colour. Static service and objective markers may be public from the start under retained rules. Dynamic bodies/items/effects use the filtered view and are removed on loss of authorization, before fog shading. Do not draw all actor positions then cover them with a dark texture.

Distinguish own ward sources from shared public sources by icon and legend. Tower offline state is an authorized public POI fact. Panning, zooming or clicking a source cannot request a broader world snapshot. The Heart's public attempt sensor still publishes only its authorized footprint and facts.

## Life and frontend states

Keep Host, Join, class selection, Ready, errors, leave and restart in the same widget/copy system. Normal profile does not use the old Migration demo title. A test-only overlay labels fixtures explicitly.

On death the own build remains inspectable read-only, the return timer and retained gold loss/drop result are clear, and world commands are unavailable. Clear stale target selection and quick references for lost items. On return use the new life, restore camera follow, show protection countdown and require new inputs. No old drag or cast survives.

A terminal result closes trades, stops commands and leaves a readable winner or No acquisition result with return/restart choices consistent with existing authority. One remaining living contender is never announced as winner. Do not conceal an unavailable conquest route behind a misleading enabled button.
