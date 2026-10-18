# Known limitations

- B35 adds one repeatable courier route, real Seal ownership/theft and both Heart
  access routes to the small uncooked slice. Full crafting/profession rank and
  tower combat/progression remain deferred; their acquisition/access setup is
  labelled as local-authority fixtures. Guild E accepts directly instead of
  opening the baseline's job panel. Only the West destination is migrated.
- Wards have real placement, charge, 20 HP, sight, expiry and melee destruction.
  Purchasing replacements remains deferred.
- Sensor sight is real; its acquisition/destruction toggle is explicitly a local
  authority fixture, not tower progression parity.
- Network evidence uses a separate authority world and two clients. Missing-view
  and reordered-message scenarios exercise the receiver with publication pauses
  and previously received packets. B34 additionally runs actual engine transport
  delay/jitter and measures echoes and six action receipts per client. B35 adds
  real objective RPCs, natural death return and both claim routes. Neither is
  a capacity benchmark; snapshot latency and broader performance remain B36.
  Serialized payload bytes exclude transport framing.
- The objective probe deliberately pauses authority sampling to put two pickup
  RPCs in one arbitration batch. Its recorded acknowledgement tail includes this
  artificial delay. These few samples do not certify the 250 ms p95 target.
- Public sites use native local markers and engine primitive meshes; the 23 saved
  generated packages are unchanged. Seal drops project to nearby navigation and
  validate ground; an unavailable location falls back to Guild with a warning.
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
