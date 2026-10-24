# B73 — build, gear and preparation

24 October 2026. Based on B72 `e956db8`. The v07 integration now has the first
B71–B73 tactical review build. Full v07 remains B74–B77 work. The default frontend
and explicit v06 regression continue to use V05. All93 adopted specification
files remain byte-preserved; this document records implementation decisions.

## Natural investment, knowledge and mastery

`Build.Attributes` remains natural investment. A separate owner-only derived gear
snapshot supplies effective attributes to HP/MP, armor, attacks, legacy utility
scores and theft calculations. Natural spell requirements use the original six
attributes exclusively. Gear cannot qualify a spell gate or increase natural
carrying capacity. Attributes remain capped16; positive equipment allocations
use binding order weapon/offhand/body/feet/charmA/charmB, then STR/DEX/CON/INT/WIS/CHA,
with piece/stat2, total/stat3 and total6 caps. Owner previews show inactive excess.
Negative movement tradeoffs apply independently of that clipping.

B72's natural gate on book reading was stricter than the adopted contract. B73
corrects it: supported books can teach Rank1 before natural eligibility and across
classes. Reading still consumes one exact physical book after3s and6 quiet seconds;
known books remain. Equip and cast validate natural gates. The old v06 class-point
path cannot teach unknown skills in v07; book knowledge is required. Known skills
train at a station for2s, R2 at level3 for1 mastery point and R3 at level6 for2 more.
All point/loadout changes commit atomically. The existing53-point legacy adapters,
level1–15 thresholds and one attribute/mastery point per earned level are retained.
XP above3575 adds telemetry without more levels/points. Level changes update max
resources without filling their increase.

Five slots remain shared with economic utilities. Current implemented-ID checks
continue to reject unavailable spells; CircleIV count is capped at one. The new
equipment asset includes natural-gate/school/potency metadata for all28 definitions
without activating the14 unimplemented effects. Temporal Lock's INT13/WIS10 gate
can therefore be checked without presenting a working Temporal Lock spell.

Scholar's Sigil grants at most one effective potency rank, capped3, only for the
`control` school and declared potency fields. Trained rank continues to determine
cooldown/control duration. B72's INT multiplier on smoke/detector radius and anchor
HP is removed: radius has no INT scaling, and numeric magical damage/barrier amounts
use effective INT. Runic/Conduit spell damage applies to the delivered Arc Bolt.
Range uses one strongest cast contribution; detector size and service/Heart ranges
are unchanged. Gear MP regeneration uses integer milliseconds and carried
hundredths; maximum reductions clamp resources and re-equipping never refills.

## Equipment subset and physical identity

Eleven complete passive definitions are staged from the adopted items:
Iron Halberd, Simple Staff, Runic Staff, Conduit Staff, Bastion Plate, Archivist
Robe, Merchant Harness, Pathfinder Boots, Scholar's Sigil, Warding Eye and Weighted
Satchel. Their normal supplies/assembly are B74, so this milestone does not grant
a free normal kit or advertise unsupported item actives.

Each binding refers to the same instance in the24-slot bag. A two-handed weapon
reserves weapon and offhand with the same ID; count/weight and primary grants apply
once. Slot/type and duplicate-reference checks prevent invalid bindings. Equipped
items are marked in the bag and cannot be dropped, offered in trade or included
as outgoing recipe/capacity projections. Explicit unbinding is required first.
Bindings and three quick references add no storage or mass. Merchant capacity is
additive; duplicate Weighted Satchels use their strongest10-point capacity family.
Final capacity is checked before any unbinding or skill/attribute commit.

Warding Eye is an actual250cm LOS detector within ordinary sight authorization,
using existing smoke/wall/elevation rules. Break suppresses that tagged detector
and retains its ordinary WIS. Other supported gear passives are not marked
breakable. Pathfinder Boots use a public100cm band around native authored route
polylines as the retained-region road adapter; movement positives cap20 percent,
while Bastion's negative movement still applies. New world roads remain B76 work.

`equipment.json` and native `DA_EquipmentCatalog` are checked by
`SyncV07EquipmentData.py --check`, generated checksum contract, typed import,
re-import comparison and profile startup equality. Runtime never reads docs.
Bootstrap adds only that native asset and validates the28 retained packages plus
both tactical catalogs. Item enum values0–8 and legacy economics remain stable.

## Quiet commits, inputs and UI

V07 inventory has three actual quick cells and six bindings. Books read from the
bag and cannot occupy a combat reference. Ward charges and Null Bell require a
current reference for use; passive gear has no invented active. Generic old bind
RPCs reject in v07. A bounded versioned loadout request carries match/life/token,
item/inventory/build revisions and the equipment digest. Gear and quick changes
use the development action pipeline with6 quiet seconds and a2s stationary hold.
Owned live projectiles prevent quiet preparation even before a damage contact.
Positive damage, movement, casting/item use, release/focus loss, stale revisions,
control or death cancel the pending snapshot. Mute blocks quick preparation;
silence blocks skill preparation. Failed capacity or eligibility changes commit
nothing. Owner previews, native button holds and read-only quick footer prevent
one-click backpack activation. Physical instance cooldown metadata is copied
through adoption/transfer and never reset by binding.

Controls use profile-specific definitions. V07 defaults Q/W/E/R/D and1/2/3;
legacy keeps four skills/four references. V07 preferences use a separate
`LockdownLocalControlsV07` save. On first load compatible legacy bindings/display
preferences are copied in memory; no legacy file is overwritten. D is reserved
for the new fifth default and conflicting legacy actions relocate deterministically
before preference validation. Explicit later remaps remain supported. Item4 is
removed from v07 definitions and hidden UI; direct invalid references reject.
Fresh-input/focus safeguards remain. User preference changes save to the v07 file.

## Death and persistence

V07 death cancels preparation, trade/production and claims before physical drops.
The wallet burns ceil15 percent; bank, knowledge, natural allocation, mastery,
ordinary bound gear and ability/major/cleanse endpoints retain. Spare gear, unread
books, cargo, Null Bell and retained ward-charge consumables drop once with their
physical IDs. Respawn creates no supplies or money. New body status/barrier/aid
consent clears; HP/MP refill explicitly. Respawn endpoint is calculated once as
`D + min(90,8+2*level+floor(elapsed/120))`; later pressure cannot extend it.
Protection is3s and blocks casts/items/services/claims while allowing movement.

The retained baseline retires and removes a contender on disconnect, rather than
supporting identity-preserving rejoin. Retirement explicitly releases/drops gear
before deleting that controller, preventing orphan physical holders. This is
separate from ordinary death retention. Rejoin identity and broad restart evidence
remain inherited gaps. Future Tower Relic/keys/Fragment/Heartbreaker definitions
must apply their mandatory drop exception when implemented in B75/B76; no such
inert item is activated here. Generic active-gear/family/quality behavior remains
B74/B75 beyond the existing Bell/major-protection endpoints and physical metadata.

## Verification scope

The authority/two-independent-client fixture uses real requests and holds with
labelled diagnostic gear/books, XP, quiet setup, placement,25-gold bank funding and
a return-clock advance. It is not a normal acquisition or courier journey. It
passes46/27/26 checks; all33 rule tests, B72's59/35/34 geometry regression, B71's
34/18/15 combat regression and56 V05 authority checks pass. Native data validation
and independent ledger replays are recorded in `evidence/B73/Summary.json`.
The initial bank setup failed because the actual player wallet started at zero;
the final fixture explicitly journals diagnostic funding before real deposit.

Working FeatureLedger rows remain scoped partial. Native API/input mappings do
not certify physical/OS input, visual layouts or human acceptance. Cold packaging,
all100/150ms permutations, normal35-minute1/4-player sessions, restarts/performance,
full four-route timing and complete content/acquisition remain B74–B77 and inherited
V06-D01–D08 work. No push is requested.
