# v0.4 human review guide

No human matches are recorded by this document. Engineering results belong in
[B47](B47_IMPLEMENTATION.md); the frozen [acceptance plan](08_ACCEPTANCE_AND_PLAYTEST.md)
controls the human-review requirement.

## Start a review session

Use the cooked archive named in the current [handoff](../CONTINUATION.md). From
the repository root, run Scripts/OpenPackaged.ps1 with PackageRoot pointing to
its Archive/Windows directory and Resolution set to 1080p. It opens the native
frontend. One person hosts; the others join that host's address using the native
Join control. Each player chooses a class privately and marks Ready. The host
starts when all players are ready. One human is practice; the match cap is
25 minutes. New guests cannot replace departed contenders after start.

Record engine/build revision, package, PCs, GPU/RAM/driver, resolution and actual
quality settings. Record connection conditions and whether each participant is
an independently controlled human. Use two humans minimum across two sessions,
four where available, and at least six normal-profile multiplayer matches.
Do not use diagnostic grants, deadline changes or soak command-line flags.

## Controls and paths to review

- WASD moves; Shift sprints. Aim with the mouse and click for a basic attack.
- Hold keys 1–4 for equipped utility skills; combat skills use those same slots.
  Q and Space require equipped Ambush and Step respectively.
- P opens private preparation/books. At Guild or a town station, arm a legal
  plan and use the held confirmation after the required quiet period.
- Guild sells 40-gold books. Reading teaches rank one, takes three seconds and
  uses no SP; it does not equip or cast the skill automatically.
- Use an equipped Charter alternative at its public threshold to earn Witness.
  Each starting class has a viable route. Witness grants no currency or XP.
- E handles work and visible loot. Courier collection/delivery each hold three
  seconds, pay 25 gold/45 XP/one credit once, and use a 20-second acceptance gap.
  Towns alternate; the match budget is eight paid jobs.
- At Guild, hold Shift+E for four stationary seconds to fabricate one Seal from
  Witness, four unused credits and 100 gold. E remains available for courier work.
- Carry an earned or recovered Seal to Heart and hold E for ten seconds. Living
  unprotected contenders contest within 240 cm. The normal conquest port is
  unavailable. A recovered Seal requires no Witness.
- F places an owned ward. Equipped Analyze at public score 14 can disable watch
  sight temporarily at the control. The marked wilderness sentinel is hostile;
  peaceful residents remain nonviolent.

## Observe choices without forcing one route

Across the matches include two different builds from the same class, an actual
cross-class book learner, utility-heavy preparation and combat-heavy preparation.
Let players choose points, books, slots, towns/routes and acquisition attempts.
Record both theft success and failure if they occur; do not fabricate outcomes.

For each match retain a row with: session/match ID, build/package, actual human
count, starting classes (collected privately after play), point/book/slot changes,
times to each level, Witness method, positive/negative checks, routes, deaths and
waiting time, source losses, acquisition attempts, result and technical issues.
Leave unavailable observations blank and mark interrupted matches explicitly.

Ask each person which capability surprised them, which prepared skill displaced
another, and what they would change after a setback. Record raw comments and
small-sample counts. Review repeated loadouts, effectively free utility, lost
counterplay, privacy inferred from appearance, travel choices and downtime.
Do not infer equal class win rates or 60–90 minute enjoyment from this small
v0.4 review.

## Acceptance boundary

Human review is pending until real sessions and observations exist. The separate
90-minute technical soak is a stability/load fixture and cannot substitute for
human strategy, pacing, build diversity, counterplay or enjoyment. Remote delivery
and dedicated packaging also require their own actual evidence.
