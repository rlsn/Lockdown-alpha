# Roadmap and project governance

## Delivery sequence

| Stage | Main purpose | Evidence |
| --- | --- | --- |
| v0.1 and v0.2 | Historical web foundations and objective experiment | Inspect the actual implementation; user reports v0.2 complete |
| v0.3 | Unreal migration and representative scaffold | Pinned build, native UI, geometry, authority, fog and packaged clients |
| v0.4, B47 checkpoint closed | Class-based character building in a small connected region | Recorded engineering delivery; human acceptance and deferred validation remain open |
| v0.5 | Consistent UI and control | Click navigation, authorized selection, coherent HUD, inventory, quick items and real bounded trade |
| Integration and parity | Port remaining world, economy and AI systems | End-to-end routes under normal rules, accurate ledger |
| Long-session prototype | Develop the full 60–90 minute experience | Human pacing, recovery, acquisition and long-run technical evidence |
| Squad experiment | Add eventual four-player groups | Team sight, ownership, progression and acquisition rules |
| Public readiness | Operate a fair native game | Measured capacity, recovery, matchmaking, moderation and costs |

No stage promises a date or staffing level. Hero-roster expansion is removed. The old requirement to finish every v0.2 port before changing character design is superseded by the user's new direction; preserve working foundations and inventory remaining parity honestly.

## Development discipline

Audit the actual delivered v0.4 repository and evidence before v0.5. The implementation and B47 evidence are present here; the [v0.5 baseline review](../v05/B50_BASELINE_REVIEW.md) records the current inspection and fresh packaged smoke check. Preserve working gameplay, document deviations and repair only prerequisites required by the sprint. Do not rebuild a working platform or upgrade the engine unnecessarily.

Use versioned character, skill, interaction and world data. Keep legality and outcomes authoritative. A class is a starting preset, not a large new hero framework. Adopt ability infrastructure deliberately and avoid two parallel damage or cooldown systems.

## Risks

| Risk | Review response |
| --- | --- |
| DEX or INT dominates | Review combined power, frequency and mobility rather than one coefficient |
| Utility bypasses slots | Test hotbar, context menus, books, old keys and RPC entry points |
| Random checks become repeated free rolls | Bind commit, costs, cooldown, target identity and RNG receipt |
| XP comes from loops | Award meaningful completion receipts and limit repeat sources |
| Appearance reveals a build | Audit replication, UI and proxy metadata |
| Peaceful growth is only decorative | Require a zero-kill route under real acquisition rules |
| Short testing is mistaken for long pacing | Separate compressed integration, technical soak and full-session human evidence |
| Selection reveals a hidden build | Test every inspector, tooltip, minimap and trade payload against authorized views |
| UI click also issues a world order | Test input routing, focus loss, held release, remapping and modal layers |
| Item shortcuts duplicate ownership | Keep quick slots as references and trade as atomic inventory transactions |
| Large world becomes empty travel | Measure journey decisions, service density and alternative paths |

The owner decides creative scope and stage advancement. Developers own implementation and honest evidence; testers supply observations. Record decisions, tuning versions, source revision, actual checks and unresolved limitations. Mark Passed, Failed or Not run accurately.

Human validation is separate from engineering acceptance. If people are unavailable, complete feasible engineering work and mark strategy and enjoyment pending. Do not invent participants or substitute AI completion for human evidence.
