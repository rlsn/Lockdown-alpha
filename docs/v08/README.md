# Project Lockdown v0.8 — traits, loadouts and bounded elemental combat

Implementation specification • 25 October 2026 • revision 2 (B80 adoption; B80–B86 build IDs)

Rivers remain permanently traversable. Water makes characters Wet; a direct Frost hit can apply a 600 ms stun with a 6000 ms victim-wide reaction lock. Frozen river surfaces slow movement by 30% and never stun by contact or change navigation.

The loadout uses six shared active slots, seven body equipment bindings and four physical accessory slots. Weapon and armor activations occupy a shared active slot. Accessories activate through their own slot. Each class has exactly one intrinsic trait; signatures remain replaceable learned actives.

The package contains:

- 52 active abilities: 28 retained and 24 new.
- 80 items: 40 retained and 40 new.
- 20 recipes, nine class/legacy trait definitions and ten bounded reactions.
- 68 new acceptance cases plus 55 inherited cases.
- JSON catalogs, Unreal integration contracts, a build prompt, reference model checks and the original source archives.

Legacy economic utilities, book wrappers and objective/material definitions are additional audited adapters, outside these content counts.

Read 00 decisions → 01 loadout → 02 traits → 03 combat → 04 rivers/fields → 05 reactions → 06 abilities → 07 items → 08 acquisition → 09 authority/import/UI → 10 world/victory → 11 build scope → 12 acceptance → 13 coding prompt. 14 explains provenance and inheritance; 15 provides loadout and encounter examples. The data directory contains active tuning; schemas and tools contain specification contracts and checks. Templates record implementation evidence.

Run `python3 tools/check_spec.py` from this directory, or use its absolute path. See MODEL_VALIDATION.md for completed checks. This is a specification package, not an Unreal build. Engine, networking, AI, performance and human tests remain Not run; no game repository or engine was supplied.

Preserved integration defaults: 35 minutes, 1–4 contenders, level 15, 24 carried instances, 53 starting natural points, one equipped capstone, and four routes to the same ten-second Grand Heart claim. Full 60–90 minute sessions and four cities remain future design. No new gold rewards or free equipment are introduced. All 52 abilities and 80 items are complete-release scope, implemented through B80–B86. B80 audit/import is the first build stage; B81 implements slots/traits. See [the B80 baseline review](B80_BASELINE_REVIEW.md) for actual source progress and provenance reconciliation.
