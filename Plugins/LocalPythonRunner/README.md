# Local Python Runner

`Config/runner.json` binds this repository's project and `Scripts` folder using paths relative to the configuration file, so another checkout does not inherit a developer's drive letter.

For a machine-specific binding, copy the configuration to `Config/runner.local.json` and change its paths. The local file is ignored by Git and takes precedence. Absolute bindings created by the installer remain supported.

Status and every execution verify the actual project. Execution still requires an inspected entry-file SHA-256, an explicit expected project and a unique persistent request ID. Configuration does not execute scripts on editor startup.
