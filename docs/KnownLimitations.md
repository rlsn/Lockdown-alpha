# Known limitations

- Compiler and Framework SDK blockers are resolved. Native modules build with
  x64 MSVC 14.44.35229 and all four native rule tests pass. Full gameplay gates
  remain pending; a working native build does not establish migration acceptance.
- Generated binary assets cover B31 foundations only. Move/Aim actions exist in
  the generator, but their actual mappings and
  controller bindings are pending B32. No functioning frontend/UI is claimed.
- No native character, networking, fog renderer, life loop, combat, economy or
  Heart interactions have been implemented. No playable game/package is delivered.
- RunTwoClients.ps1 is pending the actual host/join/server implementation. A launcher
  around an empty map would not satisfy the network acceptance cases.
- Most native U01–U40 gameplay gates remain not run; see TestReport.md.
  Dedicated packaging remains conditional.
- The web baseline remains intact. Fresh browser recording, integration reruns,
  native visual comparison, delayed-network testing and human usability review
  are pending.
