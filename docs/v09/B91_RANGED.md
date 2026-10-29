# B91 — ranged basic attacks against finite saplings

29 October 2026 · native source `279d434308c09c8ad8914120f6f010e657f49b27` · B91 remains in progress

A ranged basic attack can now acquire an observed, live sapling in its normal
attack cone and weapon reach. It launches the existing traveling basic projectile, aimed90cm above the
supporting floor. This keeps flight and contact within the existing ground-query
support limits and resolves damage after travel.
The projectile may ignore its target trunk for collision, while other geometry
and existing projectile interception remain effective. Cleared targets invalidate
in-flight follow-ups. The finite object authority applies HP loss and synchronizes
collision, sight and navigation removal; destruction produces no Fiber.

This uses the ordinary attack of the active build and adds no key, world verb,
free tool or consumable. Harvest still requires a real bound Forestry Knife.
The current B91 endowment grants no bow; full-map acquisition remains B92 work.
Ranged fallback is limited to saplings here; other ranged prop damage and spell
foliage damage remain separate work. Character and tactical-field projectile
paths retain their existing life/identity checks.

The two-owner native scenario retains its melee, source/pointer, harvest, item
binding, interruption, death/return and finite-conservation checks. It then stages
a diagnostic ranged build, attacks from250cm through actual basic-attack RPCs,
observes a traveling projectile before damage and clears80HP without a reward.
A diagnostic sight wall prevents a subsequent sapling from being selected or
harmed by directional ranged attacks. No paid bow acquisition, mid-flight wall
insertion, rendered usability or complete reference encounter is claimed.

An initial run timed out at the new phase17 because the test RPC bound still
accepted only16 phases. The fixture bound now includes18; the failed run is
retained as diagnostic history rather than acceptance. Further diagnostic runs
verified a grounded firing pad and exposed the proxy-center endpoint exceeding
the150cm ground-query tolerance. Final flight and contact use trunk height;
character and field endpoints are unchanged. Gameplay RPCs are unchanged.

See [frozen evidence](evidence/B91Ranged/README.md) for source, native reports,
inherited field/economy regression and build logs. This increment changes no
starting physical endowment, saved map asset or supplied release file. Full
contender/economy recovery, broader paid reactions, scenery, layered fields and
human/density acceptance remain open.

Verification:53 two-owner source/sapling checks,55 native rules and616 inherited
field/projectile assertions pass. The economy ledger replays414 receipts across
9 accounts,323 unique items and4482 gold. Both full-unity targets pass. All148
protected assets and59 supplied files retain their original bytes.
