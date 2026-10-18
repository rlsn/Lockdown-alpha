# B32 movement checkpoint guide

Build with Scripts/BuildEditor.ps1 and validate content with
Scripts/BootstrapAssets.ps1 -Mode Validate. OpenEditor.ps1 starts the editor on
L_MigrationSandbox; Play starts directly in the movement world. For the frontend
flow open L_Frontend before pressing Play.

For a separate interactive game window, run Scripts/OpenStandalone.ps1. This
uses uncooked content and the editor runtime at 1280x720, disables editor-only
MCP toolsets for that process and saves a local log under Artifacts/. It is not
a packaged build.

The game startup map is L_Frontend. Choose **Host movement demo** to enter the
sandbox as a listen server. Join accepts a host name or IPv4 address with optional
port (1-65535). Default: 127.0.0.1:7777. Remote views/fog are not implemented;
joining is not a full multiplayer acceptance demonstration.

| Input | Current behavior |
| --- | --- |
| WASD | Camera-relative movement; normalized diagonals |
| Left Shift | Hold to sprint at 300 cm/s; walk at 180 cm/s |
| Mouse | Face a valid walkable-ground hit; invalid hits keep facing |
| Wheel | Zoom 1100-1700 cm; adds no vision source |
| H / Escape | Toggle menu; PIE can reserve Escape to stop play |
| Resume | Return to movement |
| Return to menu | Leave world and open frontend |
| Exit | Quit game / stop play session |

The raised path has a walkable ramp and a cliff that cannot be stepped up
straight. Walls and tree placeholders block movement. Eight residents wander
with the same humanoid presentation as the player. No role labels, damage,
objectives or rewards exist in this checkpoint.

Scripts/RunMovementCheck.ps1 writes repeatable JSON correctness evidence under
ignored Artifacts/. It drives a standalone NullRHI world, not a rendered frame
rate or real network latency benchmark.
