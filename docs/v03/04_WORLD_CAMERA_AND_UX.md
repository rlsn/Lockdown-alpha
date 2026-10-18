# Three dimensional world camera and UX

## Coordinate and scale contract

The proposed conversion is two Unreal centimetres per v0.2 world pixel. This is an explicit scale choice for a compact human-sized prototype, not an Unreal requirement. Preserve gameplay seconds, HP, damage, XP and currency. Do not multiply time or damage by the spatial scale.

```text
UE_X_cm = (old_x - 1200) * 2
UE_Y_cm = (800 - old_y) * 2
UE_surface_Z_cm = logicalVisionHeight * 200
```

The Heart becomes `(0, 0, 0)` on the ground. The old 2400 by 1600 extent becomes 4800 by 3200 cm. Positive Unreal Z is up. Imported authoring positions describe feet or ground surfaces, not capsule centres. Character capsule centre is offset above the floor by its half-height.

| v0.2 value | Proposed Unreal value |
| --- | --- |
| Walk 90 pixels per second | 180 cm per second |
| Sprint 150 | 300 cm per second |
| Radius 14 | Capsule radius 28 cm |
| Basic reach 48 | 96 cm centre to centre in the ground plane |
| Assassination reach 56 | 112 cm in the ground plane |
| Dash distance 180 | 360 cm over the same 200 ms |
| Body sight 360 | 720 cm |
| Ward sight 280 | 560 cm |
| Tower sight 300 | 600 cm |
| Heart shared sight 180 | 360 cm |
| Claim distance 60 and contest 120 | 120 cm and 240 cm |
| Work or shop distance 60 | 120 cm |
| Seal pickup 40 and ward placement 48 | 80 cm and 96 cm |
| Vision cell 40 | 80 cm |

Squared spatial thresholds multiply by four, not two. Aim vectors convert direction without applying positional offsets. Keep all conversions in one helper with round-trip tests. If the real v0.2 build uses different numbers, record that discrepancy before adopting these reference defaults.

## Terrain and collision

Use real walkable ground, one raised plateau at 200 cm, visible cliff faces, and ramps at least 400 cm long with sufficient width for the capsule. Character standing height is approximately 180 cm. Walkable ramp slope must be supported by the configured movement component; a 200 cm rise over 400 cm run is about 26.6 degrees. Keep step height below the cliff rise so the character cannot step onto high ground directly.

There is no jump input. Characters remain on walkable surfaces, and dash follows the same collision and height restrictions. Ordinary actors do not block one another, preserving the v0.2 crowd policy. Static walls, trees and tower bases block movement. Define separate ground-aim and vision channels rather than treating every render mesh as the same obstacle. [Epic collision reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-response-reference-in-unreal-engine).

The physical world and vision data must agree. A decorative tree that looks opaque must block sight; an invisible collision box must not create unexplained fog. Validate that low sight cannot see across the plateau, high sight can see down, and walls and trees still block elevated sources. Real 3D geometry does not by itself reproduce the intended high-ground rule: logical source height and server sight policy remain explicit.

## Camera and aiming

Use a perspective top down camera with a proposed pitch of minus 60 degrees, yaw minus 45 degrees, spring-arm length 1400 cm and horizontal FOV 55 degrees. These are initial presentation values. Allow a bounded 1100–1700 cm zoom, with no camera rotation in the first scaffold. Camera settings do not alter authorized visibility.

WASD is camera-relative in the ground plane so W moves toward screen up. Record that control mapping change from the web reference. Face toward the cursor's projected ground point independently of travel direction. Use an aim trace that ignores characters, foliage and roof tops, and intersects valid walkable ground or the intended ramp surface. If the trace fails, preserve the last valid horizontal aim; do not snap to world origin or produce invalid vectors.

Configure acceleration and braking explicitly. The converted walk and sprint values are steady-state speed caps, not a claim of identical first-frame acceleration to the web integrator. Compare start and stop distance in the demo review and record any material handling difference.

The server receives bounded movement and aim intent rather than trusting a client teleport or cursor-selected hit. Input uses Enhanced Input actions and contexts. Separate gameplay, menu and held-interaction behavior, including release and cancellation. [Epic Enhanced Input reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine).

Opening a panel must stop that mouse click from firing a weapon. Closing it restores the expected cursor and movement mode. Escape closes a panel before opening the pause or leave menu. On focus loss release held actions and neutralize movement; the server also expires stale held-interaction state. A native menu does not pause the shared match.

## Presentation quality gate

The user is moving platforms because poor graphics and UX obstruct evaluation. A plain capsule-only scene is acceptable for early debugging, not the final demo. Deliver a coherent humanoid placeholder with a readable silhouette and facing, restrained materials, grounded scale, simple locomotion, attack commitment and impact feedback. Use original modular shapes or an available properly licensed asset; production character art is not required.

All crowd roles use the same presentation pool and scale. No human-only overhead name, player outline, unique material or special animation may identify a quiet opponent. The local player may have a private self marker. Diagnostic damage dummies can be labelled in a separate test area; they are not evidence of crowd concealment.

Use simple, consistent lighting, visible ramps, identifiable public stations and clearer covered routes. Fade camera-occluding scenery only as a presentation effect; it must not create new sight or reveal hidden dynamic proxies. Avoid relying on expensive rendering features for the greybox to be readable. Declare graphics settings and hardware in performance reports.

## Native UX contract

The frontend offers Host migration demo, Join by address, a small controls panel and connection failure feedback. After joining, show the map and controls without requiring a console command. Display a persistent Migration demo label and distinguish diagnostic fixtures from normal interactions.

The HUD shows self HP, action cooldowns, gold and XP, life state, return timer, active courier stage, Seal ownership, access route, claim progress and the current source-visibility legend. Only display other actors' information when allowed by the observation contract; do not add enemy health bars as an incidental UI improvement.

An interaction prompt names the visible object, required action, range failure or prerequisite, and whether the action is held. A three-second collection, three-second delivery and ten-second claim all show server-confirmed progress and a clear cancellation. A rejected request gives a concise reason. A successful delivery visibly changes the private balance exactly once.

Use E for contextual interaction, Q for assassination, Space for dash, F for ward placement, Shift for sprint, left click for basic attack and a help key or button for controls. Retain future medkit and upgrade slots as inactive labelled controls only if they improve comprehension; do not draw active buttons for unported behavior. Keybind labels should come from the mapping data.

## Fog display

Show three states: currently visible, explored but unseen, and unexplored. Static POIs may be marked on the map from the start, as in the product plan. Explored terrain must not retain moving figures, shadows, attached effects, footsteps, target selection or nameplates. Dynamic entity removal is enforced before shading, not by a dark overlay alone.

The minimap can inspect a distant owned ward or public source. Panning does not grant sight. Explain which sources are shared and which are private. Use shapes or text in addition to colour for availability, damage, channels and fog legends. Essential warnings need a visual equivalent even when sound is enabled.

## User review procedure

At 1280 by 720 and 1920 by 1080, ask a reviewer to host or join, move around a ramp, aim and attack, start and cancel a job, place a ward and explain a region's visibility. No coaching or console operation should be needed for those ordinary actions. Record where the interface caused hesitation before adding more gameplay content.

This is a presentation and usability review, not a claim that the full game's strategic balance has been tested. Compare recordings with the web build to confirm that the migration improves readability without silently changing combat timings or goal rules.
