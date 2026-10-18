# Known limitations

- B33 establishes private observation for the small uncooked slice. Combat, dash,
  assassination, death/return, courier, Seal and Heart transactions remain B34/B35.
- Wards have real placement, charge, HP, source, expiry and removal state. Basic
  attack integration, including damaging wards, comes with B34 combat.
- Sensor sight is real; its acquisition/destruction toggle is explicitly a local
  authority fixture, not tower progression parity.
- Network evidence uses a separate authority world and two clients. Missing-view
  and reordered-message scenarios exercise the receiver with publication pauses
  and previously received packets; they are not measured engine packet-loss/latency
  or capacity benchmarks. Serialized payload bytes exclude transport framing.
- Native fog/minimap and humanoids need the complete human readability review
  at both target resolutions with combat cues and interactions present.
- Cooking/packaging, independent packaged clients, performance, delayed gameplay,
  complete reset/isolation and full acceptance remain B36. Dedicated packaging
  still requires a compatible source-engine lane; only an installed engine is set up.
- Standalone -game processes disable editor Python MCP toolsets; the normal editor
  keeps them enabled. Commandlet map loading can emit navigation teardown warnings;
  runtime navigation is checked independently.
- Full population, economy/balance, room-code discovery and authenticated resume
  parity are deferred. The original web repository remains unmodified.
