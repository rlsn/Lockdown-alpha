# Combat, life and private vision guide

Build with Scripts/BuildEditor.ps1 and validate content with
Scripts/BootstrapAssets.ps1 -Mode Validate. OpenEditor.ps1 starts the editor on
L_MigrationSandbox; Play starts directly in the movement world. For the frontend
flow open L_Frontend before pressing Play.

For a separate interactive game window, run Scripts/OpenStandalone.ps1. This
uses uncooked content and the editor runtime at 1280x720, disables editor-only
MCP toolsets for that process and saves a local log under Artifacts/. It is not
a packaged build.

The game startup map is L_Frontend. Choose **Host gameplay demo** to enter the
sandbox as a listen server. Join accepts a host name or IPv4 address with optional
port (1-65535). Default: 127.0.0.1:7777. Private remote views and fog are implemented for this small slice; cooked-client
and full gameplay acceptance remain separate.

| Input | Current behavior |
| --- | --- |
| WASD | Camera-relative movement; normalized diagonals |
| Left Shift | Hold to sprint at 300 cm/s; walk at 180 cm/s |
| Mouse | Face a valid walkable-ground hit; invalid hits keep facing |
| Left click | Commit one basic strike: 150 ms windup, 20 damage, 750 ms cadence |
| Q | Quiet rear assassination: 500 ms warning, 60 damage, 18 s cooldown |
| Space | Authority dash: 360 cm over 200 ms, seven-second cooldown |
| F | Place an owned ward within 96 cm on legal nearby ground |
| Wheel | Zoom 1100-1700 cm; adds no vision source |
| H / Escape | Toggle menu; PIE can reserve Escape to stop play |
| Resume | Return to movement |
| Return to menu | Leave world and open frontend |
| Exit | Quit game / stop play session |

The raised path has a walkable ramp and a cliff that cannot be stepped up
straight. Walls and tree placeholders block movement. Eight residents wander
with the same humanoid presentation as the player. Combat and scheduled returns
are real; objective transactions and rewards are the next stage. No role labels
identify human-controlled bodies.

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
not a packaged game or a human usability assessment.

## Combat and return

Q needs one second of quiet preparation, no current exposure and the rear of a
target that has neither dealt nor received damage in four seconds. The target's
orange warning appears immediately. Turn toward the attacker, leave reach, dash
away or deal damage before impact. Sprint input during the windup prevents the
assassination hit; movement remains capped at walking speed. A failed committed
attempt still consumes its cooldown. Basic attacks keep their accepted aim.

The HUD shows only your HP and cooldown deadlines. Orange marks danger/hits,
pale marks return protection, and amber marks exposure on permitted bodies.
Death shows your exact lost gold and fixed return countdown. Your own surviving
wards and shared tower still provide sight. Return creates a new body with 100 HP
and four seconds of protection; gameplay actions are locked during protection.
Death gives no free ward charge. Resident kills give no gold or XP.

Run Scripts/RunCombatCheck.ps1 for the native world checks and
Scripts/RunCombatNetworkCheck.ps1 for two separate client processes. Add
-RoundTripMs 100 or 150 and -JitterMs 25 for transport delay. Add -Rendered to
open one diagnostic client for live review (RenderRole A=720p, B=1080p).
These are controlled automation scenarios, distinct from a two-person playtest.
