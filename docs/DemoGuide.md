# Combat, objectives and private vision guide

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
| E | Accept work or pick up a visible Seal; hold for collection, delivery or claim |
| Wheel | Zoom 1100-1700 cm; adds no vision source |
| H / Escape | Toggle menu; PIE can reserve Escape to stop play |
| Resume | Return to movement |
| Return to menu | Leave world and open frontend |
| Exit | Quit game / stop play session |

The raised path has a walkable ramp and a cliff that cannot be stepped up
straight. Walls and tree placeholders block movement. Eight residents wander
with the same humanoid presentation as the player. Combat and scheduled returns
and objective transactions/rewards are real. No role labels
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

The local authoritative host's menu includes **Authority fixtures (setup only)**,
then **Toggle shared public sensor**.
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

## Courier, Seal and Heart

Public minimap labels are G (Guild at -400,100), W (West station at -1280,800)
and H (Heart at 0,0), in Unreal centimetres. Walk to within 120 cm on clear ground.
At Guild press E to accept one private contract. At West station hold E still for
three seconds to collect, then return to Guild and hold E for three seconds to
deliver. The confirmed receipt pays exactly 25 gold and 15 XP once. Acceptance can
repeat after twenty seconds. Two deliveries reach level two; upgrade choices and
full courier rank/crafting are deferred. Death cancels an unfinished package/job.

H opens the host menu. Enter the labelled authority fixtures and seed one Seal at
Guild. This supplies acquisition only. Resume, approach the visible gold item
within 80 cm and press E. Carried inventory appears only on its owner's HUD;
the carrier gets no public body marker. A combat death or departure drops that
same item near the body on valid ground, independent of the twenty-second corpse.
Another contender can recover it through E. Public notices give only a district.
The seed button cannot create a second item in the same world.

The Heart requires either all four defence flags cleared, or a carried Seal.
The fixture menu has separate West/East/South/Central toggles; these configure
access and do not simulate tower combat. Clear all four for the ordinary Assault
route, or keep them active and carry the seeded Seal for the Seal route. At Heart
hold E still for ten uncontested seconds. Another unprotected living contender
within 240 cm contests even without access; residents do not. The HUD displays
confirmed progress and public contest state. Release, stale input, movement,
range loss, damage or combat resets progress. A held claim grants shared sight
around Heart, lingering two seconds after interruption.

One result ends competitive play. Only a Seal-route win consumes the item. Having
the last living body never awards victory; the twelve-minute deadline without a
valid claim produces No claim. Return to menu and host again for a fresh world;
an in-world reset and full isolation verification remain B36.

Run Scripts/RunObjectiveCheck.ps1 and Scripts/RunObjectiveNetworkCheck.ps1.
The network runner supports -Scenario Assault or Seal (default both),
-RoundTripMs 100/150, -JitterMs 25 and -Rendered -RenderRole A/B (720p/1080p).
It uses diagnostic poses, a one-batch pickup queue pause, a lethal-HP setup and
the labelled seed/flags; all courier, theft, holds and final receipts use actual
owning-client RPCs. These setup actions do not establish crafting/tower parity.
