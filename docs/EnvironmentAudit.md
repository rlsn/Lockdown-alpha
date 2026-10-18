# Environment audit

Observed 18 October 2026, Asia/Shanghai. Local inventory was collected with
`Scripts/Preflight.ps1`, engine files, registry association and `vswhere`.
Absolute engine and compiler locations are retained only in ignored
`Artifacts/*-Preflight/Environment.json`; no shared engine path is prescribed.

| Item | Observation |
| --- | --- |
| OS | Windows 11 Home Chinese edition, 10.0.26200, build 26200 |
| CPU | AMD Ryzen 9 7945HX; 16 cores, 32 logical processors |
| Physical memory visible to Windows | 16,450,076 KiB (approximately 15.69 GiB) |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU |
| Driver | Windows driver version 32.0.16.1742 |
| GPU memory | WMI reports a saturated 32-bit AdapterRAM value; actual VRAM not established |
| Project drive | Approximately 185.6 GiB free at audit time |
| Engine | 5.8.3, CL 58210709, compatible CL 55116800, `++UE5+Release-5.8` |
| Distribution | Installed build (`Engine/Build/InstalledBuild.txt` exists) |
| Editor tools | UnrealEditor.exe, UnrealEditor-Cmd.exe, Build.bat, RunUAT.bat all exist |
| C++ tools | Visual Studio Build Tools 2026 18.1.1; MSVC family directory 14.50.35717 |
| Actual compiler binary | Product version 14.50.35721.0; file version 19.50.35721.0 |
| Other VS entry | Build Tools 2022 17.7.4 registered; no VC/Tools/MSVC directory found |
| Windows SDK | 10.0.22621.0; header, x64 library and resource compiler paths verified |
| Native build prerequisites | Bundled .NET SDK launches UBT; engine and editor already run; compiler is rejected |
| Web baseline | 74d3d3e884bf4d8130e1596148b45e50a409039c; clean checkout |
| Git/LFS | Git 2.54.0.windows.1; Git LFS 3.7.1 |
| Script host | PowerShell 7; baseline Node 22.16.0 |

## Actual failure

`Scripts/BuildEditor.ps1` invokes the installed engine's Build.bat with the
LockdownEditor Win64 Development target, `-NoHotReloadFromIDE` and `-Unattended`.
The unattended run completed with exit code **6**, `OtherCompilationError`:

```text
UnrealBuildTool has banned the MSVC 14.50.0-14.50.35722 toolchains due to compiler issues.
```

The local engine's `Engine/Config/Windows/Windows_SDK.json` says this family had
internal compiler errors resolved with **14.50.35723**. The directory family name
is not the actual compiler patch. The generic UBT recommendation mentions family
14.50.35717, so follow the ban boundary and actual cl.exe product version instead.
No ban list or engine source was changed. No compiler was installed automatically.

The initial build omitted `-Unattended` and waited after the diagnostic. That
task-owned UBT process was terminated, then rerun unattended to obtain the real
failure code. No editor process was terminated.

Official branch requirements were cross-checked against Epic's
[5.8 release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=en-US).
For patch-level compiler acceptance, the installed 5.8.3 UBT rules and actual
failed invocation above are the controlling evidence.

## Required next action

Update the existing Build Tools 2026 C++ toolset through Visual Studio Installer
to MSVC **14.50.35723 or later within the 14.50 family**. Then rerun preflight and
the Editor build. A different compiler family must satisfy the installed engine's
rules and be recorded before changing the lock. SDK reinstallation is not
currently indicated. A full IDE is not required.

Cold build, UHT parsing, commandlet execution, cooking and package readiness remain
unverified. MCP connectivity is independent of C++ compiler readiness.
