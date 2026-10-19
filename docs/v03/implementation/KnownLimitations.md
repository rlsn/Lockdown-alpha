# Known limitations

- B35 adds one repeatable courier route, real Seal ownership/theft and both Heart
  access routes to the small migration slice, now available in a Windows package. Full crafting/profession rank and
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
  a capacity benchmark. B36 adds packaged clients and separate measured soak evidence.
  Serialized payload bytes exclude transport framing.
- The objective probe deliberately pauses authority sampling to put two pickup
  RPCs in one arbitration batch. Its recorded acknowledgement tail includes this
  artificial delay. These few samples do not certify the 250 ms p95 target.
  The separate B36 ten-minute soak passed at249.60/241.13ms p95 with893/896
  receipts; the rendered client's margin is narrow. See TestReport.md for hardware,
  actual echo RTT and the measured small-slice conditions.
- Public sites use native local markers and engine primitive meshes; the 23 saved
  generated packages are unchanged. Seal drops project to nearby navigation and
  validate ground; an unavailable location falls back to Guild with a warning.
- Native fog/minimap and humanoids need the complete human readability review
  at both target resolutions with combat cues and interactions present.
- The packaged three-process topology uses a headless listen authority and two
  remote clients. Its local connection is a nonparticipating diagnostic fixture;
  it still occupies a connection slot. This is not a full-capacity or dedicated
  Server target test. Dedicated packaging requires a compatible source engine.
- The initial Zen-backed staging attempt failed; packaging uses file-backed cook
  output with IoStore/pak instead. This disables the newer Zen incremental-cook
  dependency lane, not runtime containers or gameplay features.
- Automated restart, performance and readability evidence does not replace human
  usability acceptance. Snapshot age uses the replicated GameState clock and does
  not establish synchronized one-way latency. WAN/NAT discovery, packet-loss
  resilience beyond the declared receiver tests and remote LFS delivery are unverified.
- Standalone -game processes disable editor Python MCP toolsets; the normal editor
  keeps them enabled. Commandlet map loading can emit navigation teardown warnings;
  runtime navigation is checked independently.
- Full population, economy/balance, room-code discovery and authenticated resume
  parity are deferred. The original web repository remains unmodified.
