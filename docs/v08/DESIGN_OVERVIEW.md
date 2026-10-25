# v0.8 at a glance

| Decision | Implementation |
|---|---|
| Six common actions | Learned combat, utility and profession skills share the bar with body equipment activations |
| Seven body bindings | Head, torso, legs, feet, gloves, main hand, off hand |
| Four accessories | Active, passive or consumable; each occupies one physical binding |
| Ten active actions maximum | Six shared actions plus four accessory activations |
| One class trait | Intrinsic exception to item-only persistent passives; signature skills still cost slots |
| Navigable rivers | No swimming, bridge dependence, ice collapse or navigation rebuild |
| Wet + direct Frost | 600 ms stun, shared six-second victim reaction lock |
| Frozen river surface | 30% contact slow, no contact stun |
| Bounded environment | Ten reactions, finite cell and lifetime budgets, no recursive propagation |

The extended catalog contains 52 active abilities, 80 items, 20 recipes and nine trait definitions covering six teaching templates and three known legacy classes. There are 68 new acceptance cases plus 55 inherited cases.

Economy, death, privacy and all four Grand Heart access routes remain integrated. Ordinary environmental damage does not cancel a surviving ten-second claim; an actual Wet–Frost stun does. Preemptive guard, dry routes, cleansing and vision manipulation provide counters.

All new numbers are prototype defaults. This archive provides implementation contracts, data and a build plan; Unreal runtime behavior and balance still require validation.
