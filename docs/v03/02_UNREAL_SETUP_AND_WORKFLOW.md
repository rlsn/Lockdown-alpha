# Unreal setup and development workflow

## Preflight record

Create `Docs/EnvironmentAudit.md` before engine-specific implementation. Record Windows version, CPU, RAM, GPU and driver, available disk space, Unreal distribution and exact patch, engine root, compiler and MSVC toolset, Windows SDK, required build prerequisites, project path and the v0.2 repository revision if available. Do not infer an installation from a shortcut or a generated solution file.

Choose one installed, supported, non-preview Unreal Engine 5 patch and pin it. If no engine is present, document the missing dependency and the proposed version; do not download a different major or minor engine silently. Use Epic's compatibility page for that exact engine branch rather than copying a generic Visual Studio version. The official page currently has different requirements by Unreal release. [Epic setup reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine).

The audit must locate the engine's editor, command-line editor, build script and Automation Tool and verify a C++ editor build. Include the Game development with C++ workload and the compatible compiler and Windows SDK. An IDE extension is helpful but is not proof that compilation works. Record missing components precisely and continue independent source and configuration work where useful.

## Version lock

Create `Docs/EngineVersion.md` and `Config/PlatformLock.json` with engine major, minor, patch, distribution type, tested toolchain, supported target and plugin list. Associate the `.uproject` with that engine. Resolve the local engine root through `LD_UE_ROOT` or a documented local settings file excluded from source control. Do not commit a user's absolute engine location or force an engine upgrade while opening the project.

Different engine builds may use different association identifiers. Document how another machine associates the same pinned version. Commit source and assets, not the engine installation. Record the engine changelist or build identifier when available. The existence of current documentation for a newer branch is not a project upgrade instruction.

## Project creation and repeatability

Start from a small Blank C++ game project or an existing compatible Unreal scaffold. Name the project and runtime module `ProjectLockdown` unless the repository already has a valid project name. Keep the code-first core small and expose deliberate presentation settings to Blueprint. A Blueprint-only project is insufficient for the required testable authority and automation contract.

Generate a real `.uproject`, build targets, module rules and configuration. Generate binary maps and assets through Unreal Editor or an editor-only bootstrap commandlet. Do not fabricate `.uasset` or `.umap` files by writing arbitrary bytes, rename text files to those extensions, or deliver redirectors as missing content. The bootstrap must save real packages and be safe to rerun without duplicating actors or overwriting hand-edited assets without an explicit force option.

First cold-build the Editor target, then run the asset bootstrap, then start the editor and PIE tests. Close the editor for reflected C++ layout changes and perform a cold rebuild when Live Coding state could conceal a problem. A Live Coding success alone is not release evidence. Small visual edits may iterate in the editor, with a clean final build afterward.

## Required developer scripts

| Script | Responsibility |
| --- | --- |
| `Scripts/Preflight.ps1` | Discover or validate the pinned engine, compiler, SDK and project; write diagnostics without installing software |
| `Scripts/BuildEditor.ps1` | Build the Editor target, capture the actual exit code and log |
| `Scripts/BootstrapAssets.ps1` | Invoke the editor-only generator and validate saved maps and essential assets |
| `Scripts/OpenEditor.ps1` | Open the project using the selected engine |
| `Scripts/RunTests.ps1` | Run the named automation suite and inspect its result artifact, not only process startup |
| `Scripts/RunTwoClients.ps1` | Launch the documented local network scenario and preserve process-specific logs |
| `Scripts/PackageWin64.ps1` | Build, cook, stage and package a Development game to a checked output directory |

All scripts take explicit paths, quote spaces correctly, propagate failures and document arguments. Never delete a computed staging directory unless its resolved path has been checked against the intended task output root. Start background helpers with hidden windows by default; visible game clients are intentional interactive test windows. Keep logs in an output directory with timestamps and test names.

Use the selected engine's build and automation tools. The package script should use the installed branch's supported UAT BuildCookRun arguments. Epic distinguishes compiling code from cooking content and staging a runnable package; successful compilation alone does not prove a packaged game works. [Epic packaging reference](https://dev.epicgames.com/documentation/unreal-engine/packaging-your-project).

## Multiplayer build lanes

**Required lane A.** A Windows Development game package can host a local listen session and another independently launched game process can join. Also run two remote client processes against a server world for the owner-view privacy tests, so the host's canonical memory is not mistaken for a restricted client. The server world may be an editor server instance or another supported local server process. Record the exact tested topology.

**Conditional lane B.** Provide a dedicated Server target and a build/run recipe where the installed engine supports it. Epic's dedicated-server tutorial requires a source engine build and a C++ multiplayer project. Do not promise that adding a target file makes a launcher installation capable of the tutorial's packaged server build. [Epic dedicated server reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine).

If that prerequisite is absent, mark packaged dedicated-server validation Not run and describe the needed source-build step. It does not block the required local listen and remote-client scaffold tests. It must block any claim of production dedicated readiness. A listen host can inspect its own authoritative process; do not describe it as an adversarially secure competitive host.

## Assets and source control

Commit `.uproject`, `Source/`, `Config/`, required `Content/`, bootstrap source, scripts, tests and documentation. Exclude build outputs, derived-data cache, intermediate files, logs, IDE caches and per-user settings. Use Git LFS for binary assets when the repository supports it; document setup and verify real asset content, not merely pointer files. Do not introduce repository-wide LFS rewriting or history changes as a scaffold task.

Keep an asset manifest with path, purpose, origin, licence or owned status and whether generated. Prefer engine primitives and original modular placeholders; no paid marketplace download is needed. An optional starter mannequin is acceptable only when available and its distribution conditions are understood. Missing external art must not prevent a clean checkout from building the prototype scene.

## Codex working boundary

Make normal edits in source, configuration, scripts and data. Use Unreal itself to create, import, inspect and save binary assets. Do not require the project owner to manually repeat an undocumented sequence of editor clicks after every checkout. Give any unavoidable one-time editor action a precise reason, location and validation step.

A missing engine may prevent build and render evidence, but does not justify pretending that generated source compiled. Finish feasible scaffold work, leave logs and state exactly what remains unverified. The requested output of this package is a future development task, not permission to accept licences, purchase assets or publish a game automatically.
