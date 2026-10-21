# Sources and proposed decisions

## Source read

UI & Control, conversation 6ad829c5-f4c0-83ec-a2ac-fbb3dc38aa1b, was read on 21 October 2026. Its one available user turn is readable; the assistant response is only a content-reference placeholder, and no older page is available. No missing recommendation is reconstructed.

The user proposes a BG3/Dota-style combination with a minimap, bottom skills/HP/MP/stats, selected-unit stats, large inventories, a smaller item-use bar, inventory/trade UI and right-click automatic navigation preferred over WASD. The planning package was authored without the delivered repository. It is now integrated into the actual Unreal repository at the user's request to prepare for v0.5; the [baseline review](B50_BASELINE_REVIEW.md) distinguishes inspected source, retained B47 evidence and fresh checks.

The master plan retains hidden builds. Selecting a body can change the inspector without revealing its private sheet. Own commands remain anchored. Requested MP space does not authorize adding mana gameplay; the proposed no-mana baseline omits an unsupported bar. These are explicit reconciliations with retained rules, not claims about an unreadable assistant recommendation.

## Proposed release defaults

| ID | Decision |
| --- | --- |
| D51 | Right-click navigates/approaches; left-click selects; explicit A then click attacks |
| D52 | Q/W/E/R four skills, 1–4 four item references, F held context, Space camera; no legacy bypass |
| D53 | Target inspector switches subject while own command/status strip stays fixed |
| D54 | Opponent private fields remain Unknown; no new reveal or mana system |
| D55 | 24 carried item slots replace four unread books; no stacking/weight/equipment bonuses |
| D56 | Four quick references use owned books/ward charges, never add storage or skill slots |
| D57 | Clean-match finite-charge conversion; if no actual policy exists, carried charges drop like valuables |
| D58 | Real vendor buying and voluntary peer trade; six offered items per side and gold |
| D59 | Two-stage consent, physical reservations, unescrowed gold, atomic net-capacity transfer |
| D60 | Snapshot approach, no auto-pursuit/channel/attack and no hidden dynamic routing oracle |
| D61 | One major panel blocks world input and stops movement; explicit layered capture and focus reset |
| D62 | Rebinding, larger text, keyboard panel traversal and declared native layout tests |
| D63 | Retain actual v0.4 normal profile and gameplay; full-game duration remains 60–90 minutes |

All counts, key choices, timings, radii, response buckets, layout measurements and review thresholds are proposed sprint decisions. Adopt or record deviations deliberately against the actual delivered game. They are not presented as direct user quotations, measured balance or completed code.

The local baseline review adopts these as working implementation defaults, with two audited dispositions: retain the existing 250 ms stale-hold limit instead of the 300 ms fallback, and retain loss of unused ward charges on death rather than the conditional drop-carried-charges proposal. One starting charge becomes one physical instance at clean-match initialization, with no respawn grant. These are recorded implementation decisions preserving delivered behavior, not new direct user quotations.

## Retained versus changed

The proposed defaults retain v0.4 classes, attributes, XP, four common skill slots, ranks, subclasses, skill/cooldown/hold semantics, sight and owner privacy, paid-work limits, theft dice, earned Seal and central Heart result. Changes are input/navigation/facing conventions, UI composition, inventory capacity/charge representation, organization, optional charge death policy and voluntary trade. No hotbar rework permits extra utility skills.

No new long-session balance, full conquest port, remote acquisition, economic simulation, NPC bargaining, equipment system, broad accessibility compliance or production operations are claimed. Static diagrams and package QA establish specification quality only.

## Engineering references

The official Epic links in [07_UNREAL_CONTRACTS_AND_AUTHORITY.md](07_UNREAL_CONTRACTS_AND_AUTHORITY.md) were consulted on 21 October 2026. They describe engine facilities, not project implementation evidence. Use the installed branch, retaining the actual engine lock.

The predecessor references are frozen contracts. User-reported delivery and actual later code take precedence over treating every historical proposed coefficient as implemented. Preserve source history and record actual deviations; do not silently overwrite a working game to match an old document.
