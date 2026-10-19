# Engine and build lock

Pinned engine: Unreal **5.8.3**, changelist **58210709**, installed distribution,
branch `++UE5+Release-5.8`. Machine-readable record:
`Config/PlatformLock.json`.

The existing valid project/module name **Lockdown** is retained under the v0.3
existing-project exception. Runtime module: `Lockdown`; editor module:
`LockdownEditor`. Target defaults are explicitly pinned to `BuildSettingsVersion.V7`
and `EngineIncludeOrderVersion.Unreal5_8` from the installed engine's rules.

Updated compiler inventory: x64 MSVC **14.51.36260** and **14.44.35229**, neither
banned. The former 14.50.35721 compiler is no longer present. Windows SDK:
**10.0.26100.0**; .NET Framework SDK **4.8** is now installed and validated.
**Editor build passed**, using UBT-selected x64 MSVC **14.44.35229** and Windows SDK
10.0.26100.0. Both project DLLs have build ID **55116800**, matching the installed
UnrealEditor module manifest. The four native rule automation tests also pass.

`Lockdown.uproject` retains the owner's local engine association identifier.
Another machine should associate it with its own installation of exactly the
pinned engine (for example through the Unreal version selection shell command).
Do not copy someone else's registry GUID or infer the engine path from it.
Scripts accept `-EngineRoot`, then `LD_UE_ROOT`, then ignored local settings, then
the machine's association registry; they reject a different version/changelist.

Required runtime plugin: EnhancedInput. The user's ModelingToolsEditorMode,
ModelContextProtocol and AllToolsets plugins are retained and restricted to
Editor targets. Additional transitive AllToolsets dependencies are selected by
the engine plugin descriptor; this broad experimental editor bundle is tooling,
not a gameplay architecture dependency. Runtime module rules do not depend on it.

Game and Editor targets are supplied. A Server target is deliberately pending
the conditional source-build lane; adding a target alone would not demonstrate
that an installed engine can produce a dedicated package.
