# B86 human playtest record

Status: **Not run — no human playtest participants or measurements recorded.**
Native automation and agent screenshot inspection do not change that status.

Launch the fresh archive identified by evidence/B86/Summary.json:

```powershell
./Scripts/OpenPackaged.ps1 -PackageRoot <archive> -Profile v08_integration
```

The explicit profile hosts a v08 lobby; other players join its address from the
packaged frontend. Keep the default frontend/V05 and v07 regression results
separate. Use actual earned purchases/reading/preparation for pacing and balance
observations. Label any diagnostic setup separately.

Record package/hash, date, hardware, display resolution/OS scaling, participants,
latency, class/loadout, income/costs and relevant server/client receipts. Check:

* Explain six shared actions, item-source badges and four physical accessories.
  Remap keys, try a collision, bind a passive accessory and allocate a body active.
* Distinguish Wet, direct Frost, Frozen ground and Steam without prompting.
  Record mistaken reactions, visibility confusion and disabled-reason clarity.
* Exercise inventory search, scrolling, keyboard focus, Escape, mouse-held
  channels, focus loss, consumable empty slots and source removal.
* Observe committed-fight TTK against the provisional8–15s target, cumulative
  hard control, reaction frequency, time/gold to counters and low-level claim
  interruption. Record actual observations rather than target values.
* Try the four earned routes and record build diversity, economy/gear tradeoffs,
  readability at720p/1080p and text scaling, and any blocking UI behavior.

For each issue record reproduction, actual/expected behavior, evidence and
severity. Keep untested cases marked Not run; do not infer them from automation.
