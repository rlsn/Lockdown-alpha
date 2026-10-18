# Known limitations

- B34 adds attacks, native dash, warned assassination and the death/return loop
  to the small uncooked slice. Courier, Seal and Heart remain B35.
- Wards have real placement, charge, 20 HP, sight, expiry and melee destruction.
  Purchasing replacements remains deferred.
- Sensor sight is real; its acquisition/destruction toggle is explicitly a local
  authority fixture, not tower progression parity.
- Network evidence uses a separate authority world and two clients. Missing-view
  and reordered-message scenarios exercise the receiver with publication pauses
  and previously received packets. B34 additionally runs actual engine transport
  delay/jitter and measures echoes and six action receipts per client. Neither is
  a capacity benchmark; snapshot latency and broader performance remain B36.
  Serialized payload bytes exclude transport framing.
- Native fog/minimap and humanoids need the complete human readability review
  at both target resolutions with combat cues and interactions present.
- Cooking/packaging, independent packaged clients, broader performance/delayed gameplay,
  complete reset/isolation and full acceptance remain B36. Dedicated packaging
  still requires a compatible source-engine lane; only an installed engine is set up.
- Standalone -game processes disable editor Python MCP toolsets; the normal editor
  keeps them enabled. Commandlet map loading can emit navigation teardown warnings;
  runtime navigation is checked independently.
- Full population, economy/balance, room-code discovery and authenticated resume
  parity are deferred. The original web repository remains unmodified.
