# Known limitations

- B32 is a playable movement checkpoint. Combat, dash, assassination, death/return,
  courier, ward placement, Seal and Heart transactions are not implemented.
  Their input assets exist but have no runtime handlers yet.
- Host/Join UI exists. Canonical bodies are owner-only; residents do not replicate.
  Remote crowds, sanitized relays, fog, elevation sight, source union and
  independent-client privacy evidence are pending B33. This is not complete
  multiplayer gameplay.
- Four rule tests and seven movement checks pass. Most U01-U40 gates remain
  not run or partial. See TestReport.md. Human usability, delayed-network,
  resolution and performance reviews remain required.
- Game target builds; cooking and independent packaged clients have not run.
  Dedicated packaging is conditional on a compatible source-engine lane;
  only an installed engine is configured here.
- Standalone -game runs disable editor MCP plugins whose Python toolsets require
  an editor world. Normal editor MCP remains enabled.
- Commandlet map loading can emit navigation teardown warnings; runtime navigation
  is tested separately and all eight residents moved in the probe.
- The v0.2 repository is unmodified. Full population, balance, room-code discovery
  and authenticated resume parity are not claimed.
