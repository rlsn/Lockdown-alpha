# Project Lockdown v0.9 — Full Map Demo Specification

27 October 2026 • revision 1 • implementation specification, not an Unreal build

**Locked direction:** a 2.4 × 2.4 km world; fixed major geography with deterministic procedural detail; selected interiors (36 of 144 buildings, exactly 25%). Default full-match deadline is 90 minutes, with 60- and 75-minute validation variants. A legal earlier Grand Heart win remains possible.

The package defines Crownreach and four outer cities, six towns, 96 addressable POIs, 117 strategic route edges, permanent walkable rivers, three elevation bands, 480 functional props, 16 portable-traversal anchors, and a default population of 216 NPCs plus four humans. The full roster is 24 contenders including humans and AI; 196 NPCs are noncontenders. Human seats replace contender AI, so 216 is the four-human default, not an extra crowd above the contender roster.

Stages are **B90–B94**, as requested. Those IDs are shared by data, build gates, acceptance cases, the coding prompt, and implementation templates.

## Read and use

1. Open `atlas/01_World_Atlas.png` and `atlas/02_Crownreach_Detail.png`.
2. Read `00_DECISIONS_AND_COMPATIBILITY.md`, then the numbered design documents.
3. Use `data/` as the canonical numerical/spatial inputs. Coordinates are integer **centimeters**, +X east, +Y north, +Z up. Atlas labels are meters.
4. Read `11_CODING_AGENT_PROMPT.md` before editing the actual game repository.
5. Run `python3 tools/validate_spec.py` from this package. Inspect `SPEC_VALIDATION.json` for specification evidence; engine/network/AI/performance/human checks remain **Not run**.

`COMPLETE_SPEC.md` combines the numbered design documents. `data/pois.csv` is a quick coordinate index. `reference/Project_Lockdown_V08_Source.zip` preserves the supplied reference byte-for-byte, including its inherited v0.7/v0.6 contracts. This v0.9 package is a new deliverable; it does not claim to update a game repository or deliver the playable map itself.

`tools/rebuild_layout.py` rebuilds the authored data deterministically from seed 9001 and the frozen source. `tools/render_atlas.py` renders that data with Matplotlib. Change the generator when changing generated manifests, then rebuild, validate, render, and regenerate checksums. An Unreal builder must still fit terrain, doors, bridges, object pads and stairs to real collision/navigation and record evidence; a graph edge is not a native navmesh proof.

## Scope boundaries

The full demo uses all map systems described here and can use placeholder meshes. No photorealistic art, general rigid-body sandbox, universal destruction, dialogue generator, new ocean/swimming system, procedural per-match cities, or complete physical caravan supply chain is required. No spending, encounters, performance, balance, or completion time is proven by a static specification.

