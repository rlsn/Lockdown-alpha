# Presentation and accessibility

## One visual language

Use restrained dark panels, high-contrast text, one accent for own active commands and a different shape/border for item references. Avoid copying game art or fonts. Reuse Slot, ResourceRow, InspectorField, ItemDetail, HoldProgress, ResultToast, Modal, OfferTray and MappingLabel components across all screens. Do not leave the old debug HUD underneath.

Skill and item slots share state conventions but remain labelled Skills and Items. Skill capacity is four learned actives; inventory capacity and quick references are different concepts. Use action verbs Select, Move, Approach, Attack, Hold to use, Learn, Assign, Buy, Offer, Ready and Confirm consistently. A book tooltip says Teaches Spark, not Cast Spark.

Availability is Empty, Ready, Armed, Pending, Active, Cooldown or Blocked, with a reason. Trade reservation is a Blocked reason. Use icons/text as well as colour. Cooldowns and remaining hold time have numerical equivalents. Key labels come from the live mapping and update in prompts, hotbars, menus and help after remapping.

## Layout contracts

Reference layout is 1920×1080 with a proposed 220 px lower HUD band. At 1280×720, reflow to a 180 px lower band: compact minimap, compact inspector and a two-row own command strip. Do not cover the entire view with a fixed desktop-sized bottom panel. Own HP, actionable slots, return timer, targeting/cancellation and essential warnings remain visible.

Inventory/development/vendor/trade use one major-panel layer over the world. Only one major panel is active; switching closes the previous one safely. Trade has a confirmation sublayer. Tooltips never steal keyboard focus or intercept a drag release. Modal dimming does not pause time or hide critical incoming warnings. Panels explicitly say the match continues through a small clock/state cue rather than a repeated warning dialog.

Baseline body text is proposed 16 logical px, secondary text 14 and action targets at least 40×40 logical px. Support a 125 percent larger-text preference by reflow/scroll, not truncating essential labels. Test 1280×720 and 1920×1080 at Windows display scaling 100 and 150 percent, and 2560×1440 at 100 percent. Record actual viewport and effective UI scale; do not multiply OS and UMG scaling blindly. Clamp tooltips to the viewport safe area. Ultra-wide, 4K and controller layouts are deferred, not assumed working.

Use a visible focus ring and predictable Tab/Shift-Tab traversal in major panels. Enter/Space operates focused panel buttons, never world skills. Inventory has button alternatives to drag; trade has Add/Remove item and numeric gold fields. Escape remains reachable. Do not consume digit quick-item shortcuts while entering an amount. Full keyboard-only world play and screen-reader integration remain future scope; do not claim them from panel traversal alone.

## Wireframe references

See [wireframes/README.md](wireframes/README.md) for three original SVG diagrams: selected-unit HUD, inventory/quick slots and peer review. They establish hierarchy and information boundaries. Placeholder shapes are not final art; the required native interface needs real widgets, state and readable icons. Wireframes may use sample private own values, but selected-other fields remain Unknown.

At narrow sizes, reflow the bag grid from six to four columns and scroll its content rather than shrinking every cell. Peer trade keeps both offer directions and final summary visible; own-bag contents may scroll below. Neither Confirm nor Cancel may scroll offscreen. Final confirmation always identifies the current revision/changed-offer state in plain wording, without exposing engine or network terminology in ordinary player flows.

## Feedback and copy

| Situation | Example player-facing message |
| --- | --- |
| Hidden target invalidates selection | Target no longer visible |
| Click path rejected | No walkable route |
| Route becomes obstructed | Path blocked |
| Learned active lacks a slot | Equip this skill at a preparation station |
| A source permits sight but not action | Out of reach |
| Full inventory | Inventory full |
| Duplicate book read | Already learned; book kept |
| Changed peer offer | Offer changed. Review again |
| Stale vendor quote | Catalog updated. Review the price |
| Lost trade eligibility | Trade cancelled |
| Unavailable humanoid invitation | Unavailable |

Use one concise reason at the point of action and optional detail on request. Do not show server class names, RPC errors, private target resistance or hidden identity codes. Deduplicate receipt notifications. Expiring a toast is not expiring the underlying receipt or consent requirement.

## Interaction regression review

Inspect each screen in ready, pending, empty, full, blocked, death, loss-of-sight, network delay and terminal states. Capture native screenshots, not just editor widget previews. Check icon and text clipping, hover depth, hotbar ownership, contrast, pointer capture, keyboard focus, all four corners and modal dismissal. Record residual issues and compare the actual result against the diagrams. A consistent shell must still expose the real game rules.
