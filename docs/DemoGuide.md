# Movement and private vision guide

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
port (1-65535). Default: 127.0.0.1:7777. Private remote views and fog are implemented for this small slice; cooked-client
and full gameplay acceptance remain separate.

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
with the same humanoid presentation as the player. No role labels, combat, objectives or rewards exist in this checkpoint.

Scripts/RunMovementCheck.ps1 writes repeatable JSON correctness evidence under
ignored Artifacts/. It drives a standalone NullRHI world, not a rendered frame
rate or real network latency benchmark.

## B33 vision controls

F places the initial private ward near the cursor, within 96 cm of the body on
clear ground at the same logical level. It lasts 60 seconds. HUD feedback reports
rejection or success; no console command is needed. The minimap shows distant
ward and public sight. Light cells are current, dim cells explored and black
cells unknown. Entity markers disappear when permission is lost or sight is stale.

The local authoritative host's menu includes **Fixture: toggle public sensor**.
The tower contributes shared sight while active. This toggle is a diagnostic
acquisition/destruction fixture, not tower combat parity. Remote clients have no
fixture button/RPC. Public tower geometry is at (1200,1000) in this small layout.

Run Scripts/RunViewNetworkCheck.ps1 for the separate authority plus two-client
privacy scenario. It uses diagnostic fixed poses and reports local evidence,
not a packaged game or a human usability assessment. Combat remains B34.
