# v0.2 source audit

Baseline: **74d3d3e884bf4d8130e1596148b45e50a409039c**,
`Implement v0.2 Grand Heart objective experiment`. The separate repository was
clean before export and was not edited. Node 22.16.0 and installed locked
dependencies were used. Fresh `npm ci` and a fresh browser recording were not run.

## Observed source and data

`Config/BaselineSnapshot.json` is exported by importing the actual TypeScript
`C` and `MAP` values; it contains all source constants, map geometry and POIs,
coordinate-conversion metadata and SHA-256 digests of six inspected rule files.
It is reproducible with `Scripts/ExportBaseline.ps1` using the baseline's `tsx`
loader. No application code was copied wholesale into the Unreal runtime.

| Contract | Source observation |
| --- | --- |
| Runtime | Phaser client, Colyseus authority, TypeScript; protocol 2 |
| Scheduling | 50 ms simulation, snapshots every two ticks |
| Map | 2400 by 1600; Heart at (1200,800); map version plaza-2 |
| Movement | Walk 90, sprint 150 px/s; radius 14; dash 180 px over four ticks; seven-second dash cooldown |
| Combat | 100 contender HP, 40 resident HP; 20 basic damage, 3-tick windup, 15-tick cadence; 60 assassination damage, 10-tick warning, 360-tick cooldown |
| Death | 400 + 200*(level-1) + 100*floor(tick/2400) + 900 at/after tick 9600; gold loss ceil(gold*(20+5*(level-1))/100) |
| Return | New body identity; retained progression; 80-tick contender protection; residents return after 400 ticks |
| Courier | Three-second work and delivery; 25 gold and 15 XP; 20-second repeat cooldown |
| Claim | 200 ticks; 60-pixel interaction and 120-pixel contest radius; Seal bypass or all four tower flags down |
| Wards | 280-pixel private radius, 1200-tick lifetime, cap two, 48-pixel placement range; source removal on expiry/destruction |
| Vision | Body/owned-ward/public-tower/Heart union; low rays blocked by raised areas; high rays still blocked by walls and trees |
| Pickup arbitration | Gather eligible requests, shuffle by independent seeded transaction stream, then process; nearest drop tie breaks by item ID |
| Deadline | 14400 ticks / 720 seconds; no claim produces no winner |
| Resume | Ten-second guest reconnect is web functionality; native authenticated resume is deferred |

These observations came from `packages/shared/config.ts`, `map.ts`,
`packages/simulation/life.ts`, `vision.ts`, `objectives.ts` and inspected match/
world source. Existing baseline docs and reports are supporting historical
material, not substitute evidence for a new Unreal build.

## Rechecked results

- `npm test`: **3 files, 89 tests passed**.
- `npm run typecheck`: **passed**, exit code 0.
- Logs: ignored `Artifacts/BaselineVerification/UnitTests.log` and `Typecheck.log`.
- Existing `artifacts/v02-01-city.png` was visually inspected. It shows the web
  crowd/fog/status layout. It is a prior repository screenshot, not a fresh
  recording or proof of new runtime behavior.
- Integration, browser, long soak and delay suites were inventoried but not rerun
  in this checkpoint. Existing JSON reports remain in the baseline repository.

Baseline startup is `npm run dev` (server 2567, client 5173). Its README documents
build, integration, browser, routes and soak commands. Native work will preserve
the web checkout for side-by-side recording during presentation review.

## Initial migration decisions

Two centimetres per source pixel and Y inversion are explicit. Grounded native
Character Movement replaces the old integrator; camera-relative WASD replaces
world-axis WASD. Native rule samples and view publication will use the same 20/10
Hz rates but not claim lockstep ordering or identical acceleration. Nonviolent
residents reduce from 24 to eight under the v0.3 slice; complete economy, tower
combat, crafting and AI contender policy remain deferred. See MigrationLedger.csv.
