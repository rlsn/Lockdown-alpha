# B33 private observation checkpoint

B33 adds the observation boundary before combat. Normal movement remains native
Character Movement; a separate server director samples after physics at 20 Hz
and publishes complete owner views at 10 Hz. Hitches sample the current pose once.

## Runtime boundary

- Server-only contender records retain a private session identity and owner state.
  Only the owning controller receives match/life generation, initial HP/resources,
  active state and ward charges. These are foundations for B34/B35, not completed
  damage or economy loops. Aim and ward requests validate current match/life.
- Canonical controlled characters and PlayerState actors remain owner-only.
  Residents, wards and the vision director do not replicate. GameState carries
  public sensor availability, without canonical bodies or private records.
- Each connection has one owner-only world-view relay. Observed IDs are random
  per-connection GUIDs, independent of actor network GUIDs. Human and resident
  records share the same schema and nonreplicated humanoid proxy class.
- The bounded wire format has generation, revision, authority time, two packed
  300-byte masks, source counts and at most 32 neutral entity records. It contains
  no actor references, owner mapping, role, HP, currency or inventory. The native
  serializer rejects invalid counts and values. Tests serialize and decode it.
- Every full view replaces the visible set. Reliable revisioned concealment removes
  proxies immediately. Older full views/concealments are ignored. After 250 ms
  without a new full view, dynamic proxies and minimap records disappear and
  sight is marked stale. The next valid full view restores authorized entities.
  This is a receive-gap timeout, not a zero-latency permission guarantee.
- A request resolving an observed ID is scoped to that connection and rechecks
  present visibility. Old hidden IDs and another connection's IDs cannot resolve.

## Vision and presentation

Body sight is 720 cm, owned ward sight 560 cm and public tower sight 600 cm.
Ranges are planar and inclusive. The authored plateau footprint determines the
logical high level; the ramp stays low until its top. Low sources cannot see into
or across the plateau. Both source levels still trace opaque walls and trees.
Cell membership and exact point visibility are checked separately, as in v0.2.

Current and explored masks cover 60x40 cells of 80 cm. A generated postprocess
material shades current, explored and unknown terrain; a native minimap also
shows the three states and authorized dynamic markers. Removing a proxy removes
its primitive parts and shadows before fog shading. Camera zoom contributes no
source. The shared humanoid/palette does not label humans or residents.

F places a real authority ward within 96 cm on legal clear ground, with one initial
charge, a 60-second lifetime, 20 HP and cap two. Requests carry match/life/request
IDs, have a 4 Hz server bound, and replay a cached result for duplicates. Remote
clients do not receive the canonical ward or its owner. Attack-based ward damage
is connected in B34; expiry and removal of a zero-HP ward are already authority rules.

The public tower contributes actual shared sight. Acquisition/destruction is a
labelled fixture, toggled by the local authoritative host from the menu. Remote
clients have no fixture RPC. The migration profile is explicit in DefaultGame.ini.
The diagnostic network scenario also requires the non-shipping command-line flag.

## Evidence and limits

Run Scripts/RunViewNetworkCheck.ps1 for a separate uncooked authority process and
two restricted clients. Its nine phases cover different body views, private ward
sight, public sight/loss, reappearance, delayed-message injection, missing full
views, recovery, waiting sources and camera invariance. Each phase audits actual
client actors and captures only received permitted records. Authority tests cover
walls, trees, logical elevation, exact range, ward receipts and expiry.

The fixture teleports/fixes bodies and shortens the existing ward's expiry to
exercise expiry promptly; it does not claim a human playtest or measured latency.
Dropped full views are simulated by pausing publication, and reordering by passing
previously received messages through the real receiver. Independent packet-loss,
render performance, cooked-process and full usability measurements remain B36.

Latest executed results are in TestReport.md. Combat/life and representative
transactions remain B34/B35. No full v0.3 acceptance is claimed by this checkpoint.
