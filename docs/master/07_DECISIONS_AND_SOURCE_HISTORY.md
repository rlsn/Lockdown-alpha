# Decisions and source history

## Source record

The original source is **Game Concept Brainstorm**, conversation `6ad2e425-d87c-83ee-81fe-3cc094cb41f4`. Its readable user messages established online crowd stealth, XP and gold, random upgrades and purchases, 20–30 minute matches, solo or four-player survival, stealth becoming chaos and medium lethality with assassination counterplay.

The update source is **Character Design And Stats**, conversation `6ad308e5-4bc0-83e8-b22c-677ab7b24b7a`. All five available turns were read, with no older page reported. The readable user contributions add:

1. Camps, rare shops, fountains, random and character-specific upgrades, runes, bosses and a central claim of about ten seconds to avoid stalemate.
2. NPCs with player-like POI goals, an active city, strong objectives that resist unprepared early attackers, outer and central towers, defensive prerequisites and valuable last-hit rewards.
3. A central objective as the only victory, respawning with time and gold costs that increase with progression, fog, trees, terrain, elevation, wards and shared tower sight.
4. Lower risk nonviolent growth, peaceful public surveillance areas, more violence in poorly observed wild areas and a possible peaceful versus violent NPC split.
5. Multiple approaches to the Grand Heart, including peaceful progression while towers remain, substantial wealth, three distinct keys dropped on death or construction of a powerful artifact.

The chat reader returned the assistant responses as content-reference placeholders, not readable design text. No named character roster, approved numerical stats or additional agreement is inferred from those responses. References to another game's mechanics are design comparisons, not claims that the prototype reproduces its implementation.

The local `sources/` folder contained no readable files during preparation. Existing v0.1 handoff documents were also reviewed. No game source, running server, build result or playtest report was supplied.

## Direction register

| ID | Current direction | Status |
| --- | --- | --- |
| M01 | Accessible online multiplayer with crowd concealment and dangerous contenders | Retained |
| M02 | Medium lethality and meaningful assassination counterplay | Retained |
| M03 | 20–30 minute full game target and eventual four-player groups | Retained; prototype exceptions explicit |
| M04 | Grand Heart acquisition decides victory | Replaces last survivor victory |
| M05 | Respawn delay and gold loss create death stakes | Replaces permanent contender elimination |
| M06 | Peaceful growth must support an alternate Heart approach | New direction |
| M07 | Fog, terrain, trees, elevation, wards and shared POI sight | New direction |
| M08 | Calm public activity and danger in low-vision wild areas | New direction |
| M09 | Autonomous contenders and world activity progress the city | New direction, constrained by peaceful residents |
| M10 | Tower milestones protect direct capture and affect the map | New direction |

## Proposed release decisions

| ID | v0.2 choice | Why this first version |
| --- | --- | --- |
| P01 | One shared combat kit, no roster expansion | Isolate objective and vision effects |
| P02 | Three outer towers and one central tower | Test prerequisites and surveillance without a full production map |
| P03 | One courier profession and one crafted access artifact called a Concord Seal | Prove a peaceful bypass with explicit ownership and theft |
| P04 | Up to 12 minute free for all matches, no shrinking kill zone | Give both routes time and prevent environmental survival becoming victory |
| P05 | Eight contender slots, 24 residents and three two-creature wild camps | Reuse the foundation while adding one clear NPC threat role |
| P06 | Preserve the v0.1 combat numbers and two upgrade selections initially | Reduce simultaneous balance changes |
| P07 | Zero XP and gold for resident kills | Remove routine civilian farming incentives |
| P08 | Two elevation levels, static trees, private wards and public tower vision | Minimum meaningful sight experiment |
| P09 | Uninterrupted ten second Heart channel, contested by another living contender | Clear final commitment for either access route |

All P decisions and detailed numerical tuning are proposed implementation defaults. They are not represented as earlier explicit user approvals or validated balance.

## Superseded v0.1 policies

Do not carry forward automatic last survivor victory, permanent player death, the shrinking lethal safe circle, civilian farming rewards, irreversible corpse accumulation, eliminate-on-disconnect as a victory shortcut, or the old roadmap's immediate roster expansion. Keep room authority, movement, wall collision, combat counterplay, transaction discipline, guest sessions and server information filtering where they are implemented correctly.

## Decisions deferred

Full tower count, full match population, three-key route, independent wealth route, broad professions, powerful artifact pool, boss and rune content, character roster and stats, squads, account systems, monetization, public matchmaking and hosting remain open. A developer should not infer these from the conversation title or from this package's examples.

## Platform update on 18 October 2026

The user supplied **Compare Web And Unity**, conversation `6ad41143-649c-83e8-995c-fba17457b6f4`. All four available user turns were read. The user reports v0.2 done, calls the gameplay promising, identifies 2D graphics and UX as obstacles to serious testing, asks for 3D or 2.5D and selects Unreal. The requested v0.3 milestone is migration and scaffolding. Assistant replies remain unreadable references, so no engine patch, installed toolchain or unseen setup detail is treated as agreed.

M11 adds the user's Unreal platform choice. M12 adds the user's 3D or 2.5D presentation direction. Proposed implementation details include native Windows delivery, a code-first C++ core, top down grounded movement, a representative multiplayer and vision slice, and explicit deferral of full parity. The former browser stack and v0.3 route-expansion entry are superseded for new development. Gameplay intent, full match targets and eventual squads remain.
