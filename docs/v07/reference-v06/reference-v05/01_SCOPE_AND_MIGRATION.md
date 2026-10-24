# Scope and migration

## Audit the delivered game

Record actual v0.4 repository commit, pinned engine, supported toolchain, bootstrap, packaged launch and known defects. Inspect class selection, development, inventory and ward representation, skill activation, fog, native movement, receipts, death and normal acquisition. Preserve delivery evidence; a reported delivery is not a reason to repeat every unrelated test or assume every proposed v0.4 default was adopted.

Resolve actual deviations in a short ledger before implementing dependent work. Retain compatible established tuning; if it differs from the frozen specification, name the actual value and proposed decision. Missing movement, view or transaction prerequisites are explicit repairs, not an excuse to rewrite the game. Do not upgrade Unreal or mandate GAS/CommonUI conversion.

## Required result

| Area | v0.5 change |
| --- | --- |
| Movement | Right-click path commands replace default WASD locomotion |
| Selection | Left-click changes an authorized lower inspector, not command ownership |
| Attack | Explicit A then left-click; no accidental attack on neutral right-click |
| Skill input | Q/W/E/R map to the four equipped skills |
| Item input | 1–4 refer to four carried-item shortcuts |
| Interaction | F replaces contextual E; channels keep real held commitment |
| Camera | Free pan, minimap inspection, zoom and recenter with fixed rotation |
| HUD | One own-status/command area, selected-subject inspector, objective status and minimap |
| Inventory | One 24-slot carried container replaces the separate four-unread-book cap |
| Quick items | Four references into that container; no additional storage or skill slots |
| Vendor | Real catalog purchase into capacity, preserving the 40-gold book default |
| Peer trade | Invitation, versioned offers, two-stage consent and atomic exchange |
| Usability | Shared widgets, accurate labels, reflow, accessible feedback and native help |

This is a deliberate control migration. Remove old global Q Ambush, Space Step, F ward, contextual E and left-click basic-attack routes in the normal profile. Remapping may change physical keys but cannot restore unslotted skills or duplicate actions. Optional legacy/debug controls must be isolated and labelled; not required delivery.

## Retained game rules

Retain the actual agreed v0.4 class starts, six attributes, XP thresholds, four shared skill slots, rank and subclass costs, cooldown identity, skill timing, basic attack cadence, geometric sight, private builds, authority, life IDs, jobs, finite rewards, theft resolution, drop rules and earned Seal claim. Navigation does not buff speed, shorten channels or auto-pursue hidden actors.

The inventory increase explicitly removes the old four-book capacity, not the separate four-skill limit. Inventory space can hold unread books, ward charges and one Seal. Gold is an integer balance outside the grid. Known skills, Witness and commission credits are not tradable physical items. No match progression persists into the next match.

No MP resource exists in the proposed v0.4 baseline. Do not add a decorative MP bar, mana costs or a new combat resource merely to fill the requested HUD shape. Audit any actual resource already delivered and document its disposition before changing it.

## Bounded trade addition

Implement vendor buying and actual consensual item/gold exchange in this sprint. A static trade screen is insufficient. No resale, auction, NPC bargaining AI, item valuation formula, equipment bonuses, weight, stacking, crafting tree or trade XP is required. The only new ordinary economic transfer is voluntary peer exchange of carried books, wards, a Seal and gold.

## No incidental expansion

Keep one city, two towns and wilderness, current population, normal 25-minute profile, roster policy and existing resume behavior. Do not add a full four-city world, squads, remote Heart acquisition, conquest parity, human long-session pacing or new abilities. Consent UI does not permit sending messages outside the game.

Implement migration at a clean match boundary. No live upgrade of an in-progress v0.4 match or new persistent-save format is required. Convert finite ward charges to the exact same number of unique carried charge records; never grant extra charges to populate slots. Use the actual normal starting budget. Diagnostic infinite supplies remain fixtures. Existing clean-match state is initialized once; no lost ownership or silent item truncation is acceptable.
