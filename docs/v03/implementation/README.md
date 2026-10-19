# v0.3 implementation archive

B32–B36 engineering records for the Unreal migration slice, retained on
19 October 2026 as work moves to v0.4. The code checkpoint is `4cc9cbd`.
Use the [current handoff](../../CONTINUATION.md) and
[v0.4 scope](../../v04/01_SCOPE_AND_MIGRATION.md) for new work.

These reports describe the historical build, not v0.4 behavior. B36 packaging and
engineering checks were recorded as passing; human usability acceptance remains
pending. Dedicated Server packaging is conditional on a compatible source engine,
and the local clean-clone test did not certify remote Git/LFS delivery.

| Record | Purpose |
| --- | --- |
| [B36 checkpoint](B36_CHECKPOINT.md) | Final package, topology and verification boundaries |
| [Test report](TestReport.md) | B36 results followed by historical B35 and earlier evidence |
| [Known limitations](KnownLimitations.md) | Fixtures, missing parity and unverified acceptance |
| [Migration ledger](MigrationLedger.csv) | Actual v0.2-to-v0.3 port, fixture and deferred inventory |
| [Demo guide](DemoGuide.md) | How to exercise the existing migration build |
| [Architecture](Architecture.md) | Native authority, observation, combat and objective boundaries |
| [Asset manifest](AssetManifest.csv) | Generated packages and engine references |
| [Engine lock](EngineVersion.md) | Pinned engine and audited build prerequisites |
| [Environment audit](EnvironmentAudit.md) | Toolchain discovery and historical setup issues |
| [Baseline audit](BaselineAudit.md) | Inspected web source and exported constants |
| [Editor MCP report](MCP_TEST_REPORT.md) | Editor tooling and play/stop evidence |
| [Old continuation](CONTINUATION.md) | Pre-v0.4 handoff, retained as history |
| [B32](B32_CHECKPOINT.md), [B33](B33_CHECKPOINT.md), [B34](B34_CHECKPOINT.md), [B35](B35_CHECKPOINT.md) | Earlier implementation checkpoints |

Files were moved together to preserve sibling references. Repository paths such
as `Source/`, `Config/`, `Scripts/`, `Content/` and `Artifacts/` are relative to the
repository root, not this directory. Evidence paths and original measurements
are retained. Earlier sections may describe issues fixed by later checkpoints;
read the latest B36 results first. The adjacent [v0.3 specification](../README.md)
and its templates remain historical requirements rather than implementation proof.
