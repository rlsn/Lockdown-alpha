# Unreal MCP connection test

Test date: 18 October 2026 (Asia/Shanghai).

## Result

**Passed over direct MCP HTTP transport.** The running Lockdown editor accepted
MCP initialization, tool discovery and a read-only tool invocation. No game
implementation, map edits or asset writes were performed.

## Environment observed

- Project: `Lockdown.uproject` (currently a Blueprint project, without `Source/`).
- Installed engine: Unreal Engine **5.8.3**, changelist **58210709**, branch
  `++UE5+Release-5.8`, read from the associated engine's `Engine/Build/Build.version`.
- The project association resolved through the local Unreal registry entry.
- `ModelContextProtocol` is enabled in the project and loaded in the editor log.
- MCP URL: `http://127.0.0.1:8000/mcp`.
- Listener: loopback only, owned by the tested Unreal Editor process (PID 37900).
- Shared client configuration exists for Codex, Cursor, Gemini and VS Code,
  plus the root `.mcp.json`. These contain the loopback URL, not an engine path.

This is a connection audit, not the full environment/toolchain audit or an engine
version lock required before implementation by the v0.3 specification.

## Test sequence

| Operation | Observed result |
| --- | --- |
| Initial MCP `initialize` POST | Connection refused; no listener on port 8000 |
| Inspect installed plugin and local editor settings | Plugin loaded; saved `bAutoStartServer=True`, but no active listener |
| Run `ModelContextProtocol.StartServer` in the open editor's Cmd field | Server started without restarting the editor |
| MCP `initialize` | HTTP 200, session ID returned; negotiated protocol `2025-11-25` |
| `notifications/initialized` | HTTP 202 |
| `tools/list` | `list_toolsets`, `describe_toolset`, `call_tool` |
| `tools/call` -> `list_toolsets` | One toolset: `ToolsetRegistry.AgentSkillToolset` |
| `tools/call` -> `describe_toolset` | Schemas returned for `ListSkills`, `GetSkills`, `CreateSkill`, `UpdateSkill` |
| `tools/call` -> `call_tool` -> `ListSkills` | Successful result: `{"returnValue":{}}` (no project skills) |

Requests used JSON-RPC 2.0, `Content-Type: application/json`, and
`Accept: application/json, text/event-stream`. Requests after initialization used
the returned `Mcp-Session-Id` and negotiated `MCP-Protocol-Version` headers.
The server's initialize response contained empty server name/title/version fields;
the engine version above was independently read from the local engine build file.

## Limits and follow-up

- Scene inspection, actor creation, asset editing, Python execution, compilation
  and PIE are **not exposed by the discovered toolset**. A suitable editor toolset
  must be registered before those operations can be automated through this MCP
  server. This task did not install plugins or implement additional toolsets.
- Unreal MCP tools are not registered as native tools in the current chat. This
  test used direct HTTP requests from PowerShell against the configured endpoint.
  Native tool registration in the chat remains unverified.
- Saved auto-start was already enabled. Startup persistence has not been tested
  by closing/reopening the user's editor. If the endpoint refuses connections
  again, run `ModelContextProtocol.StartServer` in Unreal's bottom Cmd field and
  retry the connection. No restart is needed for the explicit start command.
- The editor shows an `Untitled` level, and `Content/` contains no saved files.
  There are therefore no project maps or assets to include in this initial commit.
- No C++ build, gameplay test or package run was attempted; implementation has
  not started.

## Repository setup

The initial repository includes the Unreal project/configuration, existing
master/v0.3/reference specifications, shared MCP client configuration, this
report, `.gitignore` and `.gitattributes`.

Unreal caches, intermediate files, saved/editor state, generated IDE files and
local outputs are excluded. Required source, content, build resources and World
Partition external actor packages remain eligible for source control.

Git LFS is installed on this machine. Repository-local LFS hooks are initialized,
and `.uasset`/`.umap` patterns are configured for future binary content. Other
contributors should run `git lfs install` before working with binary packages.
There are no binary assets to exercise an LFS upload/download round trip yet.

Repository initialization and the first commit are local. No remote was supplied,
so no hosting repository was created and nothing was pushed.
