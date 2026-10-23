# B60 — actual baseline and adopted v0.6 contract

23 October 2026. B60 document/source audit complete; no new game execution.
Runtime baseline: `bd86a85e440e5699bbe91627d696296cd6518e9b` (B57).
Handoff baseline: `4bcc74a2ce995e03586df295b77937d95f01a8f1` (B58).
The supplied planning package's statements that no Unreal repository was
available describe its authoring environment. This audit inspected the actual
repository. Historical specifications and source discussion copies remain intact.

## Adopted contract

Use master revision 6 and the complete current v0.6 spec, including banking/cargo,
distinct Supply/Industrial endorsements and registered public-window Charter.
The interrupted identical-certificate/private-Seal draft is superseded.
Adopt `data/economy_rules.json` schema 1 (SHA-256
`f3a272e9ddd7f3c0b6a20ee1ed727d20106cfcdc5a78a7388bbb834e2c744972`)
as the proposed tuning contract: five goods, two recipes, three 150-gold markets,
six finite Crystal charges, eight scheduled production/consumption opportunities,
one East shortage at 600–780, physical bank with ceil(2%) deposit burn, hard
60+2*(STR-10) cargo capacity, two distinct commissions, 100-gold registered
Charter, activation hold 4 / warning 30 / public opening 60 / claim 10 seconds.
Runtime data/bootstrap integration belongs to implementation, not this audit.

Retain the actual 1–4 locked humans, 1500-second match, level five, four shared
active slots, 24 unique carried instances and four quick references. Preserve
250 ms stale-held expiry / 100 ms refresh, native CharacterMovement with saved
steering epochs, Enhanced Input, UMG/Slate and authoritative filtered views.
No engine/framework migration. Installed Build.version and Config/PlatformLock.json
agree on **UE 5.8.4 CL 58902052**. Retained selected compiler is MSVC
14.44.35229 / SDK 10.0.26100.0; no toolchain was reinstalled or rebuilt here.

## Inspected implementation and required integration

| Domain | Actual v0.5 baseline | v0.6 work |
| --- | --- | --- |
| Match/life | LDGameMode owns contender records keyed by controller, stable session ID across pawn lives, class lobby and lock | Match-owned institutional state, locked-N budgets; never reset on service/pawn recreation |
| Disconnect | Logout releases trade, loses wards, drops books/Seal, withdraws, then removes contender; late rejoin rejected | Preserve this admission policy; account for retired wallet state explicitly, without adding reconnect infrastructure or fresh endowment |
| Physical bag | LDInventoryDirector / ALDItem own identity, revision, holder, slot, reservations; no stacking/weight/equipment | Add five kinds, lineage, shared final slot/weight validation, cargo drops and one reservation authority |
| Supplies | One starting ward migrated once at start; unused charge consumed on death, none on return; four books at 40 | Retain supply policy; add paid Fiber/Ore, no free raw material or Crystal/Core supplier |
| Commerce | Vendor quoted four unlimited books; trade transfers real instances and wallets with Ready plus separate Confirm | Add finite market instances/cash and stale bid/ask handling; preserve consent and temporary partner handles |
| Gold | Wallet starts zero; courier eight at 25/45XP; Sentinel two at 25/45XP; theft transfers at most 12; ceil(15%) death burn | Instrument every mint/burn/transfer, protected bank and account revisions; add bounded procurement/Supply/shortage budgets |
| NPC purses | WorldChecks lazily creates 36-gold nonhuman wallets, retains Sentinel wallet on return; eight residents plus one Sentinel exist | Explicitly enumerate existing balances once. Eight residents give 288, but a Sentinel purse can add 36. The spec's 738 total is illustrative, not an audited complete endowment. Retain this baseline purse policy and track 774 if all nine NPC purses and three markets are endowed; no return grant |
| Progress | First visits 25 XP; private Witness via equipped skill tasks; learned build/XP retained | No new craft/trade/gather/bank XP; Survey/Analyze use actual equipped cast/cooldown path |
| Objective | Courier grants legacy credits; four credits/Witness/100 gold fabricate physical Seal; carried Seal bypass | B65 must replace both direct and contextual normal paths with distinct endorsements/registered Charter/public access; no wealth-only win |
| Scheduling | Combat finalizes damage/death before inventory/vendor/development, then Region/Objectives/Trade | Add scheduled institution production then consumption and event boundaries before player economy commits; process public endpoints before claims |
| Privacy/UI | Eight-field observed mask; 22-field public schema; 20-field TradeView and three-field offered item; own bag in shared focused panel | Deliberate public institution exception only; own bank/progress/lineage private, remote prices timestamped; no human-role leak |

Principal source files: `Framework/LDGameMode`, `Inventory/LDInventoryDirector`,
`LDVendorDirector`, `LDTradeDirector`, `Development/LDDevelopmentDirector`,
`LDWorldChecks`, `Region/LDRegionDirector`, `Combat/LDCombatDirector`,
`Objectives/LDObjectiveDirector`, `Vision/LDViewTypes` and the native UI/input
implementations. Existing reservation flags are shared by physical/bag records;
craft/order reservations must use that authority, not an independent item store.

Normal frontend remains the B57/v0.5 checkpoint while foundations are incomplete.
Do not advertise a normal v0.6 economy or public Charter win until integrated.
Legacy V04 fixture behavior may remain explicitly selected; B65 must prevent it
from becoming a normal v0.6 bypass. No frozen predecessor spec is rewritten.

## Brief verification and carry-forward

`python docs/v06/tools/check_model.py`: PASS, 229 configured price states and
illustrative cost/budget/bank/weight/window arithmetic. This is specification
arithmetic, not C++ authority, network or normal gameplay proof. All three
supplied discussion files match their SHA256SUMS entries. Read the current
specifications, reference-v05 contracts and the complete three source replies.

Retained B57 evidence and limitations are in [B58](../v05/B58_HANDOFF.md).
Current cold checkout/build/cook, ten restarts, complete v0.5 normal journey,
performance comparison, on-screen OS-scale matrix and actual human review
remain unverified. All v0.6 gameplay acceptance gates remain unrun at B60.
The user directed minimal verification: do not automatically launch long suites,
25-minute sessions, cold packages or the deferred 90-minute stress. Short
incremental compilation and focused rule checks are appropriate for source work.
Unreal operations run sequentially, with at most four compiler actions and frozen
Source/Config until the operation exits.

## Next implementation

B61 first: shared goods/cargo and lineage foundations; persistent bank,
institution/node/budget identities and complete receipts/reconciliation before
market UI. Then B62 markets/supply, B63 production/skills, B64 world routines,
B65 contracts/public Heart, B66 UI and B67 explicitly scoped acceptance.
Use [FeatureLedger.csv](FeatureLedger.csv); keep templates unchanged.
