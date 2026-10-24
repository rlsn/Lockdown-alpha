# Targeting, visibility and terrain

## Geometry before perception

Use delivered server-filtered fog, elevation and occlusion. Own-body sight plus owned wards and explicitly public watch sources determine the authorized view. WIS/gear detection does not see through walls. Camera pan/minimap interaction adds no sight, body range or permission to target a hidden actor. Enemy private HP, MP, ranks, attributes, wallet, items, class and human/NPC role are not suddenly made public by combat.

Unit-target abilities require current authorized target at release and range from the caster's actual body. Skillshot ground/line/cone orders may target unseen static positions; do not return hit/miss information about hidden bodies except when a specific effect legitimately authorizes feedback. Projectiles continue to their last legal trajectory after sight loss, without hidden homing. A blocked unseen projectile may produce a world impact cue only to clients authorized to observe that location. Owner MP/cooldown consumption is always visible to owner.

Smoke is an occluder for ordinary sight and detectors crossing its boundary; a source inside may see within its own radius on the same side until terrain/other smoke boundaries block it. Overlapping smoke uses union geometry, not stacked blind debuffs. Detection removes concealment only within its actual geometric field; it does not cancel smoke, identify persistent owners or reveal through a wall. High-ground logic is the actual baseline's authored sight rules, not automatic extra damage/miss RNG. Destructible trees are optional only if actually delivered; do not label scenery destructible without authoritative collision/vision updates.

## Movement and counterplay

Walk/dash use native collision and one movement authority. Push/pull sweep along a vector and stop at first blocking geometry; never clip through walls. If displacement results in zero position change, it does not qualify as forced-movement interruption, although an independent explicit interrupt still can. Root does not stop another unit pushing you. No initial ability pushes off navigable world bounds or causes arbitrary falling deaths; stop at validated ledge safety boundary. Purpose-built traversable elevation links are allowed.

Blink/swap/gate are teleport operations with explicit valid endpoints. They may cross intervening static walls only where that ability is defined as teleport, with both endpoints walkable, capsule fitting and outside protected service collision; no endpoint inside the Heart model or blocked geometry. Grounding/root prohibit voluntary teleport and the listed swap target cases. Surface/endpoint failures return generic placement feedback when details would expose hidden units. No path solver may query hidden opponents to reveal their coordinates.

Arcane Prison creates a temporary walking collision ring with gaps around bodies intersecting its proposed wall at creation; it never traps a body by spawning inside its capsule. Ring does not block vision. Its visible attackable anchor has listed HP; killing it removes collision immediately. Teleport can cross the ring if otherwise legal. Gate endpoints do not themselves scout; sight updates when a real source arrives.

## Target audiences and FFA

Normal offensive AoE hits other contesting bodies and eligible hostile creatures, not owner, own constructs, or protected officials. It never assumes an informal ally is friendly. Offense against ordinary residents follows retained body rules but grants no farm XP/new gold; no new guard/law simulator is required. Towers/bosses are immune to hard control, displacement, hex, break, silence and mute by public category, but take damage. These exceptions appear in target previews for observed objects.

Friendly dispels require self/owned unit or recipient aid consent; this is a narrowly scoped interaction flag, not squad diplomacy. Decoys cannot collect rewards, block capture, trade, pick up keys or act as independent vision sources. Summoned agents are not required beyond explicit markers/decoys/wards/emitters.

## Information feedback

Own HUD names debuff, remaining duration, blocked actions and counter category. World VFX/sound has a defined visibility audience; casting behind fog must not serialize source identity/position to everyone. Heart warning/attempt sensors intentionally publish static objective location and timing, never private claimant build. Inspection still reads Unknown for hidden data. Damage feedback to an attacker never exposes full target HP or hidden item definitions. Restricted-client payload inspection is required, not just hiding widgets.
