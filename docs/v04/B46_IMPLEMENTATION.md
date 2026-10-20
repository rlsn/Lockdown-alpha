# B46 — Earned Concord Seal acquisition

20 October 2026. B46 is implemented and engineering-verified. Full v0.4 release
acceptance remains B47.

## Runtime behavior

At the Guild, E continues courier work. Hold Shift+E to choose fabrication;
continue holding E stationary for four seconds. The HUD shows the choice, costs,
private readiness and whether this contender has already crafted this match.
Fabrication requires a living unprotected contender, retained Charter Witness,
four unused commission credits, 100 actual gold and an empty Seal slot. It is a
basic objective interaction, independent of learned/equipped skills and their
cooldowns. Active courier work and the eight-paid-job budget remain separate.

The authority checks eligibility at start and throughout the held transaction.
Release, stale hold, movement, displacement, lost clear Guild access, damage or
death cancel with no fabrication costs. Cancellation ends this fabrication hold;
a fresh press is required. Damage is resolved before completion on the same
authority sample. Scheduled completion must be at or before the fixed deadline.
The server creates the unique item before changing balances, so creation failure
spends nothing. Successful commit consumes four credits and 100 gold, owns one
Seal and records a private once-per-match receipt. Witness and paid-job count
survive. Replay, new request IDs, dropping, death and victory cannot reopen craft.

The existing independent Seal actor retains immutable identity through ownership,
death drop, permitted observed-ID recovery and final consumption. Recovery does
not require Witness. Different contenders may each fabricate one distinct item;
carry capacity remains one, separate from unread books. Fabrication history and
item ownership are owner-private. The public game-state and eight-field observed
entity allowlists gain no fields.

Central defence flags remain 15. The actual carrier bypasses the protected gate,
holds E at Heart for ten seconds within 120 cm and can be contested by a living
unprotected contender within 240 cm. The public attempt sensor, interruption,
scheduled deadline ordering and unique terminal receipt remain in force. The
UI identifies the conquest port as unavailable. Ordinary worlds never seed Seals.

## Verification

Authority fixtures explicitly label adversarial balances, positions, clocks and
item-creation failure. The separate ordinary practice uses native travel, the
starting Survey skill, four real paid courier jobs and original timers without
grants, teleports, build overrides or clock advances. Its 18 checks passed after
32409.22 cm of actual travel. Network acquisition uses real Charter, courier,
fabrication and ownership transactions with declared travel/cooldown fixtures.
Both native 100 and 150 ms settings, with ±25 ms one-way jitter, passed 21 server,
six client-A and seven client-B checks. These are emulated transport conditions,
not WAN measurements or action-acknowledgement percentiles.

Evidence paths below are relative to repository-root Artifacts/. Engine: Unreal
5.8.3 CL 58210709. Successful reports have zero failed checks.

| Check | Result | Evidence |
| --- | --- | --- |
| Final Editor build | Passed | `20261020-232754-024-BuildEditor/BuildEditor.log` |
| Native main / alternate fabrication | 27 / 27 passed | `20261020-231805-131-Seal-Region-DevelopmentCheck/Region.json`; `20261020-231818-594-Seal-Region-DevelopmentCheck-Alternate/Region.json` |
| Native ordinary earned acquisition | 18 passed; 32409.22 cm native travel | `20261020-231831-965-Seal-Region-Normal/Normal.json` |
| Native delayed clients, 150 ms / ±25 ms per leg | Server 21 / A 6 / B 7 passed | `20261020-232806-442-Seal-Region-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Native delayed clients, 100 ms / ±25 ms per leg | Server 21 / A 6 / B 7 passed | `20261020-232939-089-Seal-Region-DevelopmentNetwork-100-25/{Server,A,B}.json` |
| Native rules, including fabrication schema rejection | 15 passed | `20261020-233123-591-Automation/Report/index.json` |
| Retained native region | 93 passed | `20261020-233156-962-Region-DevelopmentCheck/Region.json` |
| Retained native Charter/theft, preparation, skills | 56 / 61 / 62 passed | `20261020-233210-348-WorldChecks-DevelopmentCheck/WorldChecks.json`; `20261020-233223-638-Preparation-DevelopmentCheck/Preparation.json`; `20261020-233236-948-Skills-DevelopmentCheck/Skills.json` |
| Native ten-world reset | 95 passed, including dirty craft history and physical Seals | `20261020-233250-242-Seal-Region-DevelopmentReset/Reset.json` |
| Retained Heart scenarios | Main 53 / exact deadline 3 / late claim 3 / no claim 4 / withdrawal 5 passed | `20261020-233311-772-ObjectiveCheck/{Main,ExactDeadline,LateClaim,NoClaim,Withdraw}.json` |
| Native HUD at 720p / 1080p | Captured and inspected; readable courier/fabrication choice, costs and readiness | `20261020-233413-959-Seal-Region-DevelopmentUI-720/Region.png`; `20261020-233500-991-Seal-Region-DevelopmentUI-1080/Region.png` |
| Native Game target | Passed | `20261020-233522-508-BuildGame/BuildGame.log` |
| Saved assets | Validate passed; 28 packages, no asset rewrites | `20261020-233706-462-Bootstrap-Validate/Bootstrap.log` |
| Win64 Development cook/stage/archive | Passed | `20261020-233727-804-PackageWin64/Package.log` |
| Cooked main / alternate fabrication | 27 / 27 passed | `20261020-233833-117-Packaged-Seal-Region-DevelopmentCheck/Region.json`; `20261020-233837-629-Packaged-Seal-Region-DevelopmentCheck-Alternate/Region.json` |
| Cooked delayed clients, 150 ms / ±25 ms per leg | Server 21 / A 6 / B 7 passed | `20261020-233841-287-Packaged-Seal-Region-DevelopmentNetwork-150-25/{Server,A,B}.json` |
| Cooked ten-world reset | 95 passed | `20261020-234002-700-Packaged-Seal-Region-DevelopmentReset/Reset.json` |
| Cooked HUD at 720p / 1080p | Captured and inspected; readable choice, readiness and retained slots | `20261020-234013-498-Packaged-Seal-Region-DevelopmentUI-720/Region.png`; `20261020-234023-257-Packaged-Seal-Region-DevelopmentUI-1080/Region.png` |
| Cooked ordinary earned acquisition | 18 passed; 32044.64 cm native travel | `20261020-234032-006-Packaged-Seal-Region-Normal/Normal.json` |

The authority and ordinary runs preceded later probe-only changes to the
network phase bound/transition, schema negative tests and visual fixture setup;
their gameplay transaction code is unchanged. The final cooked cases verify the
completed source. Visual readiness setup grants Witness/resources and positions
the body; these screenshots are layout inspection, not earned acquisition.

Current archive: `Artifacts/20261020-233727-804-PackageWin64/Archive/Windows`.
The manifest truthfully records precommit revision `d12821d` with dirty source;
all 98 source/config files and seven payload files match its SHA-256 records.
Documentation/provenance checks cover 70 Markdown files, local links, 39 feature
rows, 48 acceptance rows and four frozen predecessor snapshots:
`Artifacts/B46DocumentationAndProvenance.json`.
The cooked clients measured echo medians of 199.63 / 183.06 ms from 356 / 355
samples. Native 150 ms setting medians were 183.09 / 183.00 ms; the 100 ms setting
measured 133.02 / 133.13 ms. These samples do not measure action acknowledgements.
Packaged authority is a headless listen world with a nonparticipating local
connection and two actual remote owners, not a dedicated Server target.

The first adversarial run exposed immediate automatic restart after displacement;
fabrication cancellation now ends its held transaction. Early network runs
exposed a probe phase bound of sixteen and a transition pause longer than hold
expiry. The Seal probe now accepts phases through twenty-five and explicitly
releases/restarts its final claim. Two death sites keep the independent dropped
items unambiguous. Failed intermediate artifacts are retained as ignored history
and do not count as passing evidence.

## Remaining acceptance

Automated practice is not human acceptance or a full-duration match. B47 still
requires the ordinary 25-minute session, a separate 90-minute technical soak,
full privacy/delivery review and human strategy/usability/pacing sessions.
Four-human capacity, dedicated packaging and remote Git/LFS delivery remain
unverified or conditional. Authenticated reconnect/resume is not retained.

B45 native and cooked captures exposed GPU-budget pressure on the 8 GB RTX 4060
Laptop GPU. These short B46 readiness captures do not show an on-screen budget
warning, but no timed frame/GPU profile was run. Rendering/profile acceptance
remains open; headless functional tests and short screenshots do not establish
usable graphics performance. No quality or warning-hiding overrides are
introduced by this checkpoint.
