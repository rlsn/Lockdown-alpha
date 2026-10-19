# B36 package and verification checkpoint

The installed-engine delivery lane builds a Windows Development Game target with
the frontend, sandbox, functional map and generated content in IoStore/pak files.
PackageWin64.ps1 writes a unique archive and a manifest of the engine lock, source
revision/dirty state, source hashes and executable/container hashes. See
TestReport.md for the executed package and test paths; an archive alone is not a
runtime acceptance result.

The final archive is Artifacts/20261019-001732-992-PackageWin64/Archive/Windows.
Launch its Lockdown.exe, or pass that folder to OpenPackaged.ps1. The separate
600-second performance evidence used the preceding measured archive identified
in TestReport.md; source and container hashes distinguish both builds.

## Launch and topology

Run Scripts/OpenPackaged.ps1 -PackageRoot '<archive>' for the native frontend.
Use Host gameplay demo in one process, Join address in another and Return to menu
to leave. Resolution may be 720p or 1080p. The packaged root bootstrap executable
is also available under Archive/Windows/Lockdown.exe. Script runners launch the
inner Game executable directly so their process handles track the actual runtime.

The three network runners accept -PackageRoot. Their packaged topology is one
headless **listen authority** plus two remote Game processes. A labelled,
non-shipping -LDRemoteAuthority fixture prevents the authority's local connection
from becoming a contender or observation source. It does not turn a Game binary
into a dedicated Server target. No remote client can invoke this setup through
an RPC. Uncooked runners retain their editor-server lane.

This distinction matters: the installed monolithic Game executable did not enter
dedicated mode with -server. The initial packaged server attempts timed out and
are excluded from passing evidence. A compatible source-engine dedicated build
remains conditional and unverified.

The first cook succeeded, but Zen-backed staging could not read the local store.
DefaultGame.ini selects file-backed cooked output instead. IoStore/pak runtime
containers remain enabled; the packaged game does not require a Zen cook server.
This changes build storage, not gameplay or the observation contract.

## Isolation and measurements

RunResetCheck.ps1 reloads the real sandbox ten times in one GameInstance. It
alternates pending death/return state with a terminal claim, leaving wards, item
ownership, private work/receipts, exploration and changed fixture flags behind.
Each new world checks its initial state and rejects prior-generation commands,
private receipts and full views. Weak references verify old actors were destroyed.
The cleanup lane requests GC every ten frames before taking memory samples;
the separate soak uses normal GC.

RunSoakCheck.ps1 uses one 1080p rendered client, one NullRHI client and the headless
listen authority. It warms for ten seconds, measures frame intervals for the next
sixty seconds, and continues network activity for ten minutes. Clients exercise
Character Movement, basic attacks and rejected contextual requests, including
requests while unavailable. Receipt accounting includes rejections and drains
before shutdown. Deaths, natural returns, resident movement and source updates
continue. This is a small-slice load, not full-population or balance evidence.

Instrumentation records combat/objective and vision work separately; the combined
sample adds each vision sample to the most recent combat sample. It also records
echo RTT, acknowledgement p95, received snapshot age, autonomous movement
corrections, actor bounds and process working-set samples. Full-view payload size
is sampled on received revisions; total native inbound bytes provide a conservative
upper bound including concealment, other replication and transport framing.
Snapshot age uses the replicated GameState clock and is not a clock-synchronized
one-way network latency measurement. Outbound native traffic is reported separately.
Hardware, driver, render settings,
frame caps, process topology and duration are saved beside the reports.

VerifyCleanCheckout.ps1 requires a committed checkpoint, makes a separate local
clone, hydrates LFS assets from the local object cache, and runs preflight, both
native builds, asset validation and rule tests without copied project binaries.
A GitHub origin is configured; this local lane does not certify remote Git/LFS delivery.
The bootstrap layout is pinned to LF in .gitattributes because its saved package
signature hashes the layout text. The first built clone exposed Windows newline
conversion; the corrected fresh clone preserves the existing signature and assets.

## Acceptance boundary

Use the actual results and limitations in TestReport.md. Computer Use review and
exported screenshots are engineering inspection, not a human usability study.
Human warning recognition, feel and comparison with the web prototype must be
reviewed before expanding to full parity. Dedicated-server packaging and a remote
repository/LFS round trip cannot be certified from this installed-engine/local-repo
environment. Deferred profession, towers, economy, population and AI scope remains
unchanged in MigrationLedger.csv.
