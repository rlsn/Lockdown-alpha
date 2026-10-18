# Known limitations

- First Editor build failed before C++ compilation: MSVC 14.50.35721 is banned by
  installed Unreal 5.8.3. Required manual toolchain update is documented in
  EnvironmentAudit.md and CONTINUATION.md.
- Native module/UHT syntax, bootstrap save/load and native automation are unverified.
  Local source/API review and PowerShell parsing are not compilation evidence.
- No generated binary assets exist yet. Bootstrap source covers B31 foundations
  only. Move/Aim actions exist in the generator, but their actual mappings and
  controller bindings are pending B32. No functioning frontend/UI is claimed.
- No native character, networking, fog renderer, life loop, combat, economy or
  Heart interactions have been implemented. No playable game/package is delivered.
- RunTwoClients.ps1 is pending the actual host/join/server implementation. A launcher
  around an empty map would not satisfy the network acceptance cases.
- All native U01–U40 gates except recorded environment/source audit remain failed
  or not run; see TestReport.md. Dedicated packaging remains conditional.
- The web baseline remains intact. Fresh browser recording, integration reruns,
  native visual comparison, delayed-network testing and human usability review
  are pending.
