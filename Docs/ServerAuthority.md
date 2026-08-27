# Server Authority: the design for the MMO shape

This document is the design for taking *Outpost: Warzone* from its 1998
peer-distributed multiplayer model to a **server-authoritative MMO**: one
authority that owns the truth about the world, and clients that send intents
and render what the authority tells them. It records where the tree already
stands, why the current model cannot be the destination, the target
architecture — **which begins embedded inside the single-player game**, with
the server separable later — the staged plan, and, in the most detail, **the
message protocol between client and server**, derived message by message from
the protocol the game speaks today.

Like [AssetPipeline.md](AssetPipeline.md), this records a design without
claiming a phase number; whether it takes one is the owner's call. The
[decisions](#decisions-for-the-owner) it is gated on are listed at the end.
Everything *new* this design names — messages, types, functions, files —
follows [AGENTS.md §1](../AGENTS.md); the `NET_*` and `NetAdd` spellings that
appear below are quotations of the legacy code being replaced, not patterns
being extended.

---

## Where the tree already points

Three earlier decisions were made with this destination named, so the starting
position is better than it looks for a 1998 codebase:

- **The transport is already client-server shaped.** Phase 5 replaced
  DirectPlay with QUIC via MsQuic behind the `Transport` seam
  ([Transport.h](../NeuronCore/Transport.h)). The host owns every connection
  and relays client-to-client traffic; reliable ordered streams and unreliable
  datagrams both exist; TLS 1.3 covers every byte. LAN broadcast discovery was
  deliberately *not* built "because the destination is a server-authoritative
  setup where a relay server owns the connections and listing games is a query
  to a known server" — `Transport::FindSessions` is the hole that query drops
  into.
- **The library split is half done.** `NeuronClient` (presentation, input,
  audio) exists and is populated; `NeuronServer` exists as a PCH shell awaiting
  "authoritative simulation, session ownership" ([AGENTS.md §2](../AGENTS.md)).
  The frame counters already moved into `GTime.h` *"because a headless build
  has a frame number without having a window."*
- **Save/load is gone** because "a local save of world state has no meaning" in
  a server-authoritative world. The world's persistence is the server's job by
  prior decision, not a question this design has to reopen.

What has *not* moved: the simulation is still inside `Outpost.exe`, coupled to
rendering in `gameLoop()` ([Loop.cpp](../Outpost/Loop.cpp)), stepped by the
variable `frameTime` (capped at `GTIME_MAXFRAME` = 166 ms), and the multiplayer
protocol above the transport is unchanged since 1998.

## The 1998 model, measured

The game's multiplayer is **distributed authority with drift correction**, and
every part of the design below is an answer to some part of it:

- **Authority is partitioned by player, not owned by anyone.** Every machine
  runs the full simulation for the whole world. `whosResponsible(player)` in
  [MultiPlay.cpp](../Outpost/MultiPlay.cpp) assigns each player — including
  every AI player — to some human's machine; that machine simulates those units
  and broadcasts the results. Nothing checks that what it broadcasts is
  *plausible*, only that it parses.
- **State, not intent, goes on the wire.** A factory finishing a droid is the
  owner broadcasting `NET_DROID` ("a new droid exists, here is its id");
  research completing is `NET_RESEARCH`; a kill is the *victim's owner*
  announcing `NET_DROIDDEST`. A modified client can therefore announce
  anything: free units, free research, other people's droids destroyed. The
  message list in [MultiPlay.h](../Outpost/MultiPlay.h) has 48 types and
  roughly 40 of them are assertions about the world rather than requests.
- **Consistency is statistical.** [MultiSync.cpp](../Outpost/MultiSync.cpp)
  ("magic happens here") broadcasts periodic `NET_CHECK_DROID` /
  `NET_CHECK_STRUCT` / `NET_CHECK_POWER` messages — position, damage and power
  at 1000/700/10000 ms intervals, budgeted by `okToSend()` against
  `game.bytesPerSec` — and receivers *snap their world to match*. Divergence
  between machines is expected, continuous and papered over; `SYNC_PANIC`
  (40 s) names the case where it wins.
- **Everyone knows everything.** Fog of war is a rendering decision.
  `visible[MAX_PLAYERS]` on every `BASE_OBJECT` is computed on each machine,
  but the full world state — every enemy position, every power level (the
  power *check is a broadcast*) — is present in every process and crosses the
  wire in the checks. A maphack is a reading exercise.
- **Object identity is minted per peer**: `(objID << 3) | player`
  ([ObjMem.cpp](../Outpost/ObjMem.cpp)) — id spaces are disjoint by
  construction because no machine can ask anyone before creating an object.
- **The session is the host.** `HostLost` is terminal by design; the world
  lives exactly as long as one player's process.

None of this is wrong for what it was: eight untrusted-but-friendly peers on a
LAN in 1998, minimising bandwidth on a 28.8k modem. All of it is wrong for a
persistent world with strangers in it.

**One thing the current model is *not* is deterministic lockstep.** That
matters, because it closes off the cheap-looking alternative: the simulation
runs on a variable timestep, uses floats throughout (Phase 10 moved angles to
float radians), and the check messages exist precisely because peers drift.
Making it bit-deterministic across machines would be a rewrite of the
arithmetic core with no test oracle. The server-authoritative design below
**does not require determinism anywhere** — only the server simulates, so
"drift between simulations" stops being a concept. That is the single biggest
simplification this direction buys.

---

## Target architecture

Two sides and a boundary — before there are two machines:

```
the server side                ── the authority ─────────────────────────────
  NeuronServer.lib             world simulation: droids, structures, combat,
  NeuronCore.lib               movement, pathfinding, power, research, AI,
                               scripts; validation of client commands;
                               per-player visibility as a replication filter;
                               fixed tick; no window, D3D or audio dependency

the client side                ── the view ──────────────────────────────────
  NeuronClient.lib             rendering, HCI, input, audio, FMV; a replica
  NeuronCore.lib               world updated from server messages; sends
                               intents, never state

Directory service  (later)     ── the front door ────────────────────────────
                               accounts and session tokens; the list of
                               running worlds/zones that answers FindSessions;
                               player stats and persistence storage
```

`NeuronCore` keeps what both sides speak: the transport, the wire protocol,
`GTime`, the script VM, resources, `Neuron::Json`. The dependency edges stay
exactly as [AGENTS.md §2](../AGENTS.md) draws them — and `Outpost.exe`
already references all three libraries, so the embedded arrangement below
needs no project restructuring.

**The two sides ship in one process first.** `Outpost.exe` carries both
halves from the day the boundary exists: the server side embedded beside the
client, the two exchanging the same encoded messages they would exchange over
a network, through an in-process transport. Where the server runs is then a
launch-time binding, walked one rung at a time:

| Rung | Server runs | The boundary is | Ships as |
|---|---|---|---|
| 1 | inside `Outpost.exe`, same thread | encoded messages through `LoopbackTransport` queues | single player, stage D |
| 2 | inside `Outpost.exe`, worker thread | the same queues, now the only thing crossing threads | optional; a scheduling change, not a design change |
| 3 | `OutpostServer.exe`, same machine | the same messages over QUIC to localhost | dedicated/testing, stage E |
| 4 | a remote machine | the same messages over QUIC | multiplayer (F) and the service (I) |

Two disciplines make the ladder real rather than aspirational:

- **The boundary always carries encoded bytes.** The embedded server hands
  the client serialized messages — never pointers into its world. The moment
  the client can reach a server object, the separation is decorative and
  rung 3 will not work. Paying the encode/decode cost in single player is
  the point: the protocol is then exercised by every developer who boots the
  campaign, not only by the rare networked session. (The `Transport` seam
  was built for a second implementation to arrive — the
  `UdpTransport`/`LoopbackTransport` sketch is [AGENTS.md R2](../AGENTS.md)'s
  own worked example — so the loopback is the concept-and-implementation
  split [Transport.h](../NeuronCore/Transport.h) planned for.)
- **The server half may not link the client half.** Stage B's shadow
  configuration (simulation TUs compiled with `NeuronClient` headers absent)
  is the standing proof, kept green from then on, that the embedded server
  stays one `main()` away from being a separate executable — the same
  `NeuronServer.lib`, not a copy that grew client habits.

### Single player is the first session

The campaign and solo skirmish move onto this architecture *first*, before
any networked mode changes. A solo game is a session with one client, bound
at rung 1: the embedded server loads the level, runs every AI player and
every mission script, and ticks the world; the client renders its replica and
sends the same commands a networked client would. Nothing in the protocol
knows the session is local. What this buys, in order of importance:

- **The AI is in from the start.** `playerUpdate`, the skirmish AI and the
  campaign's scripted opponents are simulation, so they land on the server
  side by construction — which is what later deletes `whosResponsible()`: by
  the time multiplayer flips (stage F), AI players have long since stopped
  being any client's responsibility.
- **The authority split is debugged with no network under it.** Loopback
  delivery is lossless and ordered, so every defect in stages C–D is an
  authority-split defect, never a netcode defect. The campaign is also the
  richest content in the tree — missions, transports, reinforcements,
  between-mission state, the intelligence screen — so a protocol that carries
  CAM_1A to a mission win has already carried more than a skirmish ever
  sends.
- **One code path.** `bMultiPlayer` forks 247 sites across 40 `Outpost/`
  files (measured 2026-08-27). Once solo and networked games are both "a
  session against a server", those forks collapse toward session *flags*
  (speed changes permitted, cheats permitted) instead of two parallel
  behaviours.
- **The run gate already exists.** `-window -game CAM_1A` is the tree's
  standard proof of life; stage D inherits it unchanged as its acceptance
  test, now proving the client renders a world it no longer simulates.

**Pause is removed as a feature** (owner decision, 2026-08-27). An authority
serving a session does not stop the world, so the design does not carry a
pause to un-carry later: the intelligence and design screens run against a
live world in solo play — exactly what they already do in multiplayer, where
every pause path is skipped — and the `gameUpdatePaused` /
`setGameUpdatePause` / `scriptPaused` plumbing in [Loop.cpp](../Outpost/Loop.cpp)
goes in stage D. What remains solo-privileged is **game speed**
(`gameTimeSetMod`, the KeyBind speed keys) and the **debug/cheat console**,
both demoted from direct calls into the simulation to
`ClientMessage::SessionControl` requests that a solo session's flags permit
and a service session refuses.

**What "MMO" means here, concretely.** `MaxNumberOfPlayers` is 8 and player
indices are baked into arrays, alliance matrices and `visible[MAX_PLAYERS]`
across the whole game. The realistic MMO shape for this engine is therefore
**a persistent service running many concurrent zones** — each zone a map with
up to 8 active players, plus accounts, persistent stats, matchmaking and
observers — *first*, with per-zone player count a later, orthogonal widening
(it is an array-audit project, not a networking one). The staged plan below
gets to "one authoritative server per game" quickly and builds the service
around it; nothing in the protocol hard-codes 8.

### The timing model

The server steps the world on a **fixed tick**. `GAME_TICKS_PER_SEC` is 1000
and all game arithmetic is in milliseconds, so the tick is a millisecond
quantum rather than a rate: **40 ms of game time per tick, 25 ticks per
second**, which is `Neuron::SimulationTickMs` as stage A landed it.

That number is measured rather than chosen. `BASE_DEF_RATE` in
[Move.cpp](../Outpost/Move.cpp) is 25: the rate the movement system was
written around, and the value `moveInitialise` seeds its entire frame-time
history with. A fixed tick of `GAME_TICKS_PER_SEC / 25` is therefore the one
length at which `baseSpeed` settles on exactly the `BASE_SPEED_INIT` the
movement code starts from, instead of on whatever the frame rate happened to
average. An earlier revision of this document proposed 100 ms by analogy with
the network snapshot rate and had no such backing; the two are independent
(below), and this is the one the code answers for.

`gameTimeUpdate()` no longer advances `gameTime` — it advances the *target*
`gameTime` is owed, and `Neuron::ConsumeSimulationTick()` spends that a whole
tick at a time, so the remainder rolls into the next frame instead of being
rounded away. `Neuron::MaxSimulationTicksPerFrame` bounds the catch-up at four
ticks, discarding the excess exactly as the old `GTIME_MAXFRAME` frame-time
limit did and at very nearly the same 160 ms, so a machine that cannot keep up
runs the world slowly rather than fast-forwarding it. At rung 1 that
accumulator runs inside the client's frame loop; the headless main loop will
drive the identical pair. Solo game speed is the server scaling its quantum
(today's `gameTimeSetMod`), never the client scaling a shared clock; and
`gameTime2`, which "never stops", stays what it already is — the client's own
frame clock, which is why presentation reads `frameTime2`.

**The simulation tick and the snapshot rate are separate numbers.** How often
the world steps is a simulation-fidelity question, answered above; how often
the server *tells* a client about it is a bandwidth question, answered in
stage C as "every Nth tick" per replication group. Nothing requires them to
match, and conflating them is what produced the unsupported 100 ms.

The client's world clock becomes derived: `serverTick × TickMs`, plus an
interpolation delay of ~2 snapshot intervals so entities always move between
two known states instead of being extrapolated. Rendering free-runs at frame
rate exactly as today; only the simulation stops being the client's job.

An RTS is the forgiving case for this: orders already take time to visibly
begin (turn, path, acknowledge), so 100–200 ms between click and first motion
reads as unit responsiveness, not lag. Client-side prediction is therefore
**cosmetic only**: immediate selection/acknowledge sounds, rally markers,
build-placement ghosts — never predicted unit movement with reconciliation.

---

## The plan, step by step

Each step is independently shippable and gated, in the tree's usual style.
The order runs the riskiest structural work (A–D) entirely inside the
single-player game, where the boundary can be proven with no network under it
while the current multiplayer keeps playing untouched beside it; the
networked flip (F) then binds remote clients to a server that single player
has already debugged.

### A — Fix the timestep  *(landed 2026-08-27)*

Split `gameLoop()` into `SimulateTick()` — the block from
`eventProcessTriggers` through `objmemUpdate`: AI, movement, droids,
structures, projectiles, power, features, animation state — and the
render/UI remainder. Drive `SimulateTick()` from an accumulator at the fixed
tick; the renderer interpolates positions between the last two sim states (or,
as a first cut, renders the latest state as today).

*Why first:* the embedded server needs a simulation entry point that is not a
frame handler, and the same accumulator later runs unchanged in the headless
main loop. This also retires `GTIME_MAXFRAME` clamping as a source of speed
variation.

**What landed.** `Neuron::SimulationTickMs`, `MaxSimulationTicksPerFrame` and
`ConsumeSimulationTick()` in [GTime.h](../NeuronCore/GTime.h)/`GTime.cpp`, and
`SimulateTick()` in [Loop.cpp](../Outpost/Loop.cpp) driven by

```cpp
while (Neuron::ConsumeSimulationTick())
  SimulateTick();
```

The 219 moved lines are byte-identical to what `gameLoop` ran inline, apart
from indentation — the move was done mechanically and checked that way rather
than by eye. Four things are worth carrying forward:

- **The compiler proved the boundary.** `SimulateTick()` takes the six object
  pointers and the loop counter as its own locals and nothing else; that it
  compiles is proof the simulation block never read `intRetVal` or any other
  widget state, which is the property stage B has to keep.
- **Presentation had to come off the simulation clock.** `processEffects`,
  `atmosUpdateSystem` and `processAVTile` are called from the terrain draw,
  once a frame, and were reading `frameTime`. Left alone they would have
  animated by a fixed 40 ms per *frame*, making effect and fog-fade speed
  frame-rate dependent — the exact bug the phase exists to remove, inverted.
  They read `frameTime2` now, as every other presentation site in the tree
  already did. Every remaining live reader of `frameTime` is inside
  `SimulateTick()`'s call tree, which is the gate.
- **`Move.cpp`'s rolling average is now provably redundant.** `baseTimes[]` is
  ten frames of `frameTime` averaged to derive `baseSpeed`; with a fixed tick
  every element is the same number and the average is the number. It is left
  in place — it self-neutralises, and deleting it is a simplification to make
  once the tick has been *run*, not a change to bundle with the one that made
  it redundant.
- **One latent defect died in the rewritten lines.** The owed-time subtraction
  is unsigned and `gameTime` starts at 2 against a `newTime` starting at 0, so
  for the opening frames of a level the wrap read as a four-billion-tick
  backlog and pushed `baseTime` out with it. The old code computed `frameTime`
  the same way and had the same hazard; the replacement orders the test before
  the subtraction. The dead `#else` half of `TIME_FIX` went with it rather than
  being rewritten as new dead code.

*Gate:* campaign and skirmish play at unchanged speed; `frameTime` no longer
reaches presentation code (grep gate above). `check_case` passes and
`crosscheck` is clean at 180/180 units in all four configurations — Debug and
Release, x86 and x64.

*Outstanding, and it needs a Windows run:* unchanged *speed* is not unchanged
*smoothness*. Until the renderer interpolates, world motion is 25 discrete
steps a second rather than one per frame, so a 60 fps display advances units
on two frames in five. Whether that reads as stutter is the one thing this
stage cannot answer without running it, and it is the question
[Verification.md](Verification.md) should carry. If it does, the tick is a
single named constant and interpolation is the designed answer — in that
order.

### B — Split simulation state from presentation state  *(first pass landed 2026-08-27)*

The object model mixes the two: `DROID` carries screen coords, `sDisplay`,
IMD pointers and effect state beside hp, order and position. Introduce the
discipline (not yet the library move) file by file: simulation fields and
functions must not reach presentation fields, textures, `Screen.h` or audio.

*Gate:* `tools/crosscheck.py --sim-only` compiles the candidate server units
with the `NeuronClient` include directory taken away. A unit that still
compiles reaches nothing presentational even transitively, so it could link
into a headless server today; a unit that fails names its own coupling in the
error. It is a **ratchet, not a pass/fail**: `NeuronCore` stays at zero, and
the Outpost count falls and never rises.

**What the instrument found.** Built first, before any untangling, because
"the object model mixes the two" is a description and not a work list. The
baseline was **22 of 58 candidate units client-free**; it now stands at
**41 of 58**. The useful discovery is that almost none of that was real:

- **The coupling was one hub deep.** 105 of 117 Outpost units reached the same
  ten client headers, because `ObjectDef.h` — which 17 headers and most of the
  tree include — pulled `RenderTypes.h`, and `Base.h` embeds `SCREEN_DISP_DATA`
  from `DisplayDef.h`, which pulled `IMD.h`. `ObjectDef.h` used **nothing** from
  `RenderTypes.h` itself; it was carrying it for its children.
- **Almost every object-model use was a pointer.** `iIMDShape`, `ANIM_OBJECT`,
  `AUDIO_SAMPLE` and `W_SCREEN` are stored and passed by the def headers and
  dereferenced by none of them — and most were already written in the
  elaborated `struct iIMDShape*` form, which is its own forward declaration. So
  `StatsDef.h`, `StructureDef.h`, `FeatureDef.h`, `ResearchDef.h`, `DroidDef.h`,
  `DisplayDef.h`, `MessageDef.h`, `Droid.h`, `Move.h` and `HCI.h` now declare
  what they point at instead of including the library that defines it.
- **`iVector` was the one type that had to move.** It is an anonymous struct, so
  it cannot be forward-declared, and `StructureDef.h` holds one by value. A
  world position is not a presentation concern, so it and `iPoint` moved from
  the client's `RenderTypes.h` to `NeuronCore/Types.h`, the vocabulary both
  halves already share. `SDWORD` and the `int32` they were declared with are the
  same type, so the layout is unchanged.
- **Some includes were simply stale.** `MultiPlay.h`, `MessageDef.h`,
  `Geometry.h`, `OptimisePath.cpp`, `Map.cpp`, `Scores.cpp` and `Target.cpp`
  included a client header and used nothing from it.
- **`ListMacs.h` was in the wrong library.** 70 lines of linked-list macros with
  no includes at all, sitting in `NeuronClient` and used by three `Outpost`
  files. It is shared vocabulary; it moved to `NeuronCore`.

The price is honest and paid in the open: 24 presentation units that had been
getting `RenderClip.h`, `Widget.h`, `Model.h`, `AnimObj.h` or `AudioSystem.h`
transitively now include them directly. That is the include-what-you-use half
of the same change, and it is what makes the remaining coupling countable.

**What is left is real, and it is the input stage D needs.** Seventeen units,
in three groups — no longer a hub to unpick but a list of calls to replace:

| Reaches | Units | Becomes |
|---|---|---|
| `Model.h` | Combat, Formation, OptimisePath, Projectile, Structure | the sim reads model *geometry* (`imd->ymax`, `->points`) to place muzzles and judge tall objects. Needs the model metadata the simulation actually uses split from the renderable mesh — a data split, not a message |
| `AudioSystem.h` | Feature, Move, Power, Research, Visibility | the sim plays sounds directly; becomes `ServerMessage::Effect` / `UiEvent` |
| one apiece | Droid→`Screen.h`, Order→`Input.h`, Mission→`Window.h`, Action→`Widget.h`, Scores→`PieMode.h`, Target→`RenderMatrix.h`, Map→`RenderTypes.h` | client calls that have to move to the client. `Order.cpp` reading the keyboard (`keyDown`) is the clearest layering defect in the tree and the one to fix first |

Two findings worth carrying forward. `HCI.h` could not be freed by dropping
`Widget.h` alone — `Message.h` → `MessageDef.h` → `Model.h` sits in the same
chain, so the pair had to go together or neither moved; a single-edge fix
measured as zero gain and was reverted rather than kept on faith. And an
`#include` inserted after the last one in a file can land *inside* an `#ifdef`:
`Loop.cpp` and `WarCAM.cpp` both took one that way, and only the Release
configuration caught `Loop.cpp` — which is the standing argument for checking
both.

### C — The protocol layer  *(under way; the wire landed 2026-08-27)*

Build the wire: the `Neuron::NetWriter`/`NetReader` pair and the message
definitions of the three planes below; the `Transport` seam widened from one
implicit channel to named channels; and `LoopbackTransport`, the in-process
implementation that carries encoded messages between the embedded server and
its client — the moment `Transport` grows the second implementation its
header was designed around. The legacy `NETMSG`/`NetAdd` path is left
standing beside it untouched: multiplayer still speaks it until stage F, and
re-encoding messages stage D is about to obsolete would be building something
whose only future is deletion.

*Gate:* encode/decode round-trip tests in `NeuronCoreTest` for every message,
and a loopback pair delivering all three planes end to end.

**The wire is in.** `Neuron::NetWriter` and `NetReader`
([NetWriter.h](../NeuronCore/NetWriter.h), [NetReader.h](../NeuronCore/NetReader.h))
compose and take apart a message body a named width at a time, little-endian,
with no padding and no struct layout involved. What `NetAdd` did — memcpy
whatever the caller handed it, carrying the host's padding and pointer width —
is not expressible here.

Three properties are worth naming, because they are what the later stages lean
on:

- **The format is pinned by a test, not by agreement.** A round-trip test
  passes even when both ends agree on the wrong layout, so `ByteOrderIsLittleEndian`
  asserts the actual bytes. Changing the encoding now has to be deliberate.
- **Bounds are sticky, never fatal.** A write past the end stores nothing and
  sets `Overflowed`; a read past the end yields zero and sets `Truncated`; both
  latch, so a caller encodes or decodes a whole message and asks once. Nothing
  throws. A decoder that forgets to ask gets a zeroed record, which is inert,
  rather than a half-populated one.
- **Hostile input has defined outcomes.** Every byte `NetReader` sees was chosen
  by a peer. A length that lies (65535 bytes of name, one byte supplied) is
  refused rather than followed; a name longer than the field it lands in is
  truncated into it *and the full declared length is still consumed*, so the
  fields after it stay aligned. That last one is the property a decoder silently
  depends on and would never notice losing.

These have no Windows dependency, so unlike the rest of the tree they were
**run** before they were pushed: a native harness under AddressSanitizer and
UndefinedBehaviorSanitizer, then the same cases as `NetWireTest` in
`NeuronCoreTest`, which CI executes.

**The catalogue is in too.** [Protocol.h](../NeuronCore/Protocol.h) carries
`ProtocolVersion`, the `NetChannel` enum (Session, Command, Replication,
Snapshot, Bulk — the five the tables above describe), and the `ClientMessage`
and `ServerMessage` id enums, plus `RejectReason`. The tables in this document
are the specification; that header is their spelling, so the two cannot drift
apart unnoticed.

Two rules are enforced by tests rather than by comments. The ids are **wire
values, so append, never insert** — `WireValuesAreStable` anchors the first id
of each plane and each `Count` sentinel, so an insert anywhere between them
fails the build rather than silently changing what an older peer thinks it
received. And an id off the wire is a byte a peer chose, so `IsKnown` range-
checks it: a message this build has never heard of is *distinguishable* from a
known one, and gets ignored instead of switched on as whatever it collides
with.

**No record structs yet, deliberately.** The tables sketch each message's
payload, but a record's fields are only really known when something encodes
and decodes them. They land with their consumers in stage D rather than being
guessed here — guessing forty of them now would mean rewriting forty of them
later.

**`LoopbackTransport` is in**, and with it the rung-1 boundary. A client and a
server in one process exchange messages through per-channel queues — the same
bytes they would exchange over a network, copied rather than borrowed, so
neither end can reach the other's objects instead of its bytes. The moment it
could, the server would stop being separable and rung 3 would not work; the
tests hold that down rather than trusting it.

The queues are **per channel, not one shared queue**. A single queue would
quietly promise a global order across every channel, which QUIC does not keep —
and code written against the stronger promise would break at rung 3 rather than
here, which is the worst place to find out. `ChannelsDoNotBlockEachOther` sends
on Bulk then Session and requires Session first.

What loopback does *not* model is loss and reordering: delivery here is ordered
and lossless, so the Snapshot channel's "may be dropped, the next supersedes
it" behaviour is only exercised once rung 3 puts a real network underneath.
That is the same gap Phase 5 recorded about NetTest, and it is stated in the
header rather than left to be discovered.

**`Transport` is deliberately not widened yet.** Its own header says a second
implementation is the moment it becomes a base and the QUIC one becomes
`QuicTransport` — but the second implementation of the *new* channelled API
does not exist yet, and building the base for one derived class is the ceremony
[AGENTS.md R2](../AGENTS.md) forbids. The legacy `NETMSG` path keeps using
`Transport` untouched, exactly as this stage intended. The two converge in
stage F, when the legacy protocol is deleted and a QUIC-backed sibling of
`LoopbackTransport` is what replaces it — that is the point at which there are
genuinely two implementations to abstract over.

That leaves stage C's remaining work with its consumers: the message records,
which land in stage D when something encodes and decodes them.

### D — The embedded flip: single player over the boundary  *(under way)*

The first pivot. Campaign and solo skirmish move onto the architecture: the
embedded server (the stage B TU set, ticking via stage A) loads the level,
runs every AI player and every mission script, and owns the world; the client
builds the **replica store** — objects created, updated and destroyed purely
from replication messages — and feeds the existing rendering, HCI and radar
from it. Player input stops calling simulation mutators (`orderDroid`,
`buildStructure`, research starts) and sends the **command plane**; script
calls that drive presentation (`scrAddMessage`, `scrShowConsoleText`,
`scrCentreView`, `scrPlaySound`, sequence playback) become
**`ServerMessage::UiEvent`** emissions from the server's VM; game speed and
the cheat console become `ClientMessage::SessionControl` requests; the pause
plumbing is deleted rather than carried. Object ids become server-minted;
`(objID << 3) | player` survives only inside the server.

Between D and F a networked game still runs the legacy peer model — nothing
is lost, because the simulation is still in the binary (it *is* the embedded
server), and the legacy path drives it directly while the boundary path
drives it in solo sessions. Keeping that window short is the schedule's job;
the design's job is that both fit in one executable.

*Gate:* `-window -game CAM_1A` — the tree's standard proof of life — boots
and plays to a mission win with the client's direct simulation calls severed
in boundary mode (grep gate), the speed keys working through
`SessionControl`, and a briefing FMV triggered by `UiEvent`. The `SIM_ONLY`
shadow gains its mirror image: the client TU set compiles with the
simulation mutators' headers absent.

**This stage is built in increments, not as one pivot.** It is the riskiest
change in the plan — it moves the whole game onto the boundary — so each piece
lands proven before the next depends on it, and the running game is untouched
until the pieces it needs exist.

**The handshake is in** (2026-08-27). `ClientHello`, `ServerHello` and
`ServerStart` are records in [Protocol.h](../NeuronCore/Protocol.h) with their
encoding in `Protocol.cpp`, and `TheWholeHandshakeCrossesTheBoundary` runs the
whole opening of a session — client hello, server verdict, client ready, server
start — over a `LoopbackTransport`, which is the shape solo play takes at rung
1. These are the first records to exist, and they exist now because the
handshake is the first thing that needs them: stage C deferred records
precisely so their fields would be settled by a consumer rather than guessed.

Three things the handshake decides, which the 1998 protocol decided late or not
at all:

- **It happens before anything is believed.** `NET_VERSION` and the executable
  hash smuggled inside `NET_OPTIONS` did this check *after* the lobby had been
  joined. Here nothing else is read until the verdict is in.
- **The refusal is a code, not a string.** A client has to act on
  `ProtocolMismatch` differently from `BuildMismatch`, not just print it. And
  an unknown result byte folds to a refusal rather than becoming a wild enum
  value to switch on — refusing for a reason we cannot name is still refusing.
- **The client's clock comes from the server.** `ServerHello::tickMs` carries
  the tick quantum, so the server can be retuned without every client being
  rebuilt, and a client never assumes 40 ms because its own build happened to
  say so.

`Consider` is the accept-or-refuse policy as a function with a test, rather
than an `if` inside a handler. It treats a server with no build hash of its own
as unable to check anyone's, which is exactly the solo case where both halves
are the same binary.

Next in this stage: the `Session` pair that owns this exchange and the tick,
then the first replication of a real entity, then the wiring into
[Loop.cpp](../Outpost/Loop.cpp).

### E — `OutpostServer.exe`, headless

One console `main()` around the same `NeuronServer.lib` + `NeuronCore.lib`
the embedded server already is: load `GameData` through the existing resource
system and manifests, boot a map, listen on QUIC, tick. Stage B's shadow
configuration promised this link would work; this stage is where the promise
is kept — and where CI regains a *runnable* test, which
[MigrationPlan.md](MigrationPlan.md) has wanted since NetTest was deleted: a
headless server that plays a skirmish of 8 AI players to a win on the build
agent, with no window, D3D, DirectInput or XAudio2 import in the binary.

*Gate:* that CI run, green.

### F — Multiplayer binds to the same server

The second pivot, small because D did the work. A networked game becomes: one
authoritative server — the host's embedded one at rung 1, or a dedicated
stage E process (decision 3) — with remote clients connected over QUIC
speaking exactly what the loopback client speaks. Join-in-progress is the
replication plane filling an empty replica. Deleted whole in this stage: the
legacy `NETMSG`/`NetAdd` path and the 48-type `NET_*` protocol,
[MultiSync.cpp](../Outpost/MultiSync.cpp) (`NET_CHECK_*`, `SYNC_PANIC`,
`okToSend`), `whosResponsible`/`myResponsibility`/`turnOffMultiMsg`, and the
apply-locally-then-broadcast idiom.

*Gate:* two clients and a server play a skirmish to completion; a third
client joins mid-game and gets a correct world from the snapshot join path;
killing a client never kills the world, and killing the server ends the
session cleanly on both clients.

### G — Interest management

Stop replicating what a player cannot see. The filter already exists and is
already maintained per tick: `visible[player]` on every object, driven by
`visibleObject`/`visTilesUpdate` and cheap lookups through the existing
`MapGrid`/`Cluster` systems. The replication layer sends a connection only
entities visible to (or owned by) its player, with enter/leave transitions
when visibility changes, and per-entity send priorities (owned and fighting >
visible and moving > static) replacing the old global byte budget.

This is the step that turns fog of war from a courtesy into **information
security**: after it, a maphack is impossible rather than impolite, because
the hidden half of the world never reaches the client's memory. (Solo
sessions run the same filter — its cost there is noise, and one replication
path is the point of the architecture.)

*Gate:* a packet-level assertion harness — a scripted client that verifies no
entity outside its vision set ever appears in its stream while a second
scripted client fights it.

### H — Identity and persistence

Accounts and tokens at connect (`ClientMessage::Auth` below); the local
obfuscated stats file (`NETmangleData`, `saveMultiStats`) becomes a
server-side record keyed by account; `NET_PLAYERSTATS`/`NET_SCORESUBMIT`
invert — the server computes scores and *tells* clients. The server
checkpoints zone state on its own schedule (the save-format problem died with
the save system; the server's checkpoint format is private to it and
versioned like any server storage).

### I — The service

The directory: a small server the game client queries for "what worlds/games
exist" (finally answering `Transport::FindSessions` / `NETfindGame`), for
login, and for creating a game (which allocates or spins up a zone server).
Host migration stops being a concept — `HostLost` still exists for the server
dying, but the world can be revived from checkpoint. Real certificates on the
service replace `HostCertificate`'s self-signed one; clients pin the
directory and the directory vouches for zone servers.

Stages G–I are ordered by dependency, but the points of no return are D (from
which single player *is* server-authoritative) and F (from which multiplayer
is); everything after widens the service around a shape that no longer
changes.

---

## The protocol

This is the message-by-message answer, grounded in the 48 `NET_*` types in
[MultiPlay.h](../Outpost/MultiPlay.h), the transport events in
[Transport.h](../NeuronCore/Transport.h), and the lobby flow in
[MultiOpt.cpp](../Outpost/MultiOpt.cpp). Three planes, because the three
kinds of traffic have different reliability and ordering needs:

| Plane | Direction | Channel | Contents |
|---|---|---|---|
| Session | both | reliable ordered stream 0 | hello, auth, lobby, chat, ping, start/stop |
| Command | client → server | reliable ordered stream 1 | player intents, sequence-numbered |
| Replication | server → client | datagrams + reliable stream 2 | entity enter/update/leave/destroy, player-scoped state, events |
| Bulk | server → client | one reliable stream per file | map/asset download (today's `FILEMSG` chunks) |

QUIC (already in the tree via MsQuic) provides exactly this: multiple
independently-ordered reliable streams plus unreliable datagrams, so a map
download can no longer head-of-line-block an order, and a lost position
datagram is *superseded* rather than retransmitted. The `Transport` seam
widens from `Send(reliable)`/`Broadcast` to `Send(channel, bytes)` +
`SendUnreliable(bytes)`; the game still never names QUIC — and at rung 1 the
same calls land in `LoopbackTransport`.

### Wire format

Rules, replacing the `NetAdd`/`NetGet` struct-memcpy idiom:

- **Names follow [AGENTS.md §1](../AGENTS.md).** The message ids are two
  scoped enums in `namespace Neuron` — `enum class ClientMessage : std::uint8_t`
  and `enum class ServerMessage : std::uint8_t` — with PascalCase enumerators
  (`ClientMessage::Order`, `ServerMessage::Enter`), the serializer is
  `Neuron::NetWriter`/`NetReader`, and new functions and files are PascalCase.
  The `NET_*` SCREAMING_SNAKE ids, the `send*`/`recv*` camelCase pairs and
  the `NetAdd` macro family are grandfathered legacy (§1: "not exemplars")
  and end with the code that carries them — nothing new extends them.
- Every field is written explicitly, little-endian, no padding — `NetWriter`
  and `NetReader` with width-explicit puts (`U8/U16/U32/S32/F32/String/Bits`).
  This also removes the last pointer-width and struct-layout hazards from the
  wire ([X64Readiness.md](X64Readiness.md) class).
- One-byte message type, then a protocol built for evolution: the
  `Hello` exchange carries a protocol version and the build hash (today's
  `NET_VERSION` + `NEThashVal` check); the server refuses mismatches with a
  reason string. No per-message version bytes.
- **Quantisation:** world positions as `U16` per axis (`MAP_MAXWIDTH` 256
  tiles × `TILE_UNITS` 128 = 32,768 max world units — fits with headroom),
  `U8` fractional part only where motion smoothing needs it; directions as
  `U16` binary angle (the Phase 10 rule stands: radians live in game state,
  integers live on the wire and convert at the boundary); hp as `U16`;
  object ids `U32`, server-minted, never reused within a session.
- Entity records are **schema-by-kind**: droid, structure, feature,
  projectile-burst each have one canonical enter-record and one delta-record,
  with a per-field dirty mask (`U16` bitfield) so an idle droid costs two
  bytes of mask in a snapshot it appears in at all.

### Session plane

`ClientMessage::` / `ServerMessage::` qualifiers are elided in the tables —
the direction column names the enum.

| Message | Dir | Replaces | Payload (sketch) | Notes |
|---|---|---|---|---|
| `Hello` | C→S | `NET_VERSION`, `NEThashVal` in `NET_OPTIONS` | protocol version, build hash | first bytes on the connection |
| `Hello` | S→C | — | accept/refuse + reason, server tick quantum, assigned connection id | |
| `Auth` | C→S | *(new)* | account token from directory (stage H); before H: player name | |
| `Auth` | S→C | *(new)* | account ok, persistent player id, rank/stats summary | replaces reading the local stats file |
| `GameList` | C→dir | `NETfindGame` → `Transport::FindSessions` | filters | the query Phase 5 left a seam for |
| `GameList` | dir→C | `GAMESTRUCT[]` fill of `NetPlay.games` | per game: name, address, players/max, the four `gameFlags` | keeps the browser working unchanged |
| `CreateGame` | C→dir/S | `NEThostGame` | name, map, `MULTIPLAYERGAME` options | allocates a zone server (stage I); before I: the creator's embedded server hosts it |
| `Join` | C→S | `NETjoinGame` | game id, requested colour | |
| `LobbyState` | S→C | `NET_OPTIONS` (`sendOptions`) | `MULTIPLAYERGAME` (map, type, base, alliances, limits, power), slot table (player ↔ connection ↔ colour ↔ ready), structure limits | one authoritative lobby snapshot, re-broadcast on change; replaces the options + `player2dpid` + `JoiningInProgress` + colour + alliance memcpy train |
| `LobbySet` | C→S | `NET_COLOURREQUEST`, options edits, `NET_TEMPLATE` (pre-game force) | field, value | host edits options; anyone edits own colour/ready/force |
| `PlayerJoined` / `PlayerLeft` | S→C | `Transport::Event` `PlayerJoined`/`PlayerLeft`, `NET_LEAVING` | player id, name, reason (left/kicked/timeout) | |
| `Kick` | S→C | `NET_KICK` | reason | server closes the connection after |
| `Chat` | both | `NET_TEXTMSG`, `NET_WHITEBOARD` | scope (all/allies/direct), text | server relays and filters by alliance; whiteboard drops unless wanted |
| `Ping` / `Pong` | C→S / S→C | `NET_PING`, `PingTimes` | echo token | QUIC RTT stats supplement it for the bars UI |
| `MapRequest` | C→S | `NET_REQUESTMAP` | map hash I have (or none) | |
| `File` | S→C | `FILEMSG`, `NETsendFile`/`NETrecvFile` | own QUIC stream: name, size, bytes | |
| `Start` | S→C | `NET_FIREUP` | start tick, countdown ms, map hash | clients load the level, then: |
| `Ready` | C→S | `NET_PLAYERRESPONDING` | — | server holds tick 0 until all ready or timeout |
| `GameOver` | S→C | `NET_DMATCHWIN` (unused), local win detection, `multiplayerWinSequence` | outcome per player, final scores | win/lose becomes a server statement |

Join-in-progress needs no special plane: a joiner receives `LobbyState`,
then the replication plane's normal full-enter of every entity in its
interest set — the same path as fog clearing, so `NET_PLAYERCOMPLETE`,
`NET_REQUESTPLAYER`, `NET_STRUCT`, `NET_WHOLEDROID`, `NET_FEATURES` and
`bDisplayMultiJoiningStatus`'s progress dance all collapse into "replication
from an empty replica". A solo campaign start is the same sequence minus the
directory: `Hello`, `Auth`, `LobbyState`, `Start`, `Ready`, replicate.

### Command plane — client intents

Every command carries a client **sequence number** (`U16`, wrapping) so the
server can ack, dedupe and rate-limit, and so a reject can name what it
rejects. The server answers only when the UI must know
(`ServerMessage::CommandReject { seq, reason }` — no power, not yours, not
visible, illegal placement, prereq missing, rate limited); acceptance is
visible as world change. **Anything the old protocol let a client *assert* is
here demoted to a request**, and the server performs the checks the receiving
peers never did:

| Message (C→S) | Replaces | Payload (sketch) | Server validates |
|---|---|---|---|
| `Order` | `NET_DROIDINFO`, `NET_DROIDMOVE`, `NET_GROUPORDER`, `NET_SECONDARY`, `NET_SECONDARY_ALL`, `NET_REQUESTDROID` (obsolete) | droid id list (or group id), `DROID_ORDER`, x,y, target object id, secondary mask, queue/replace flag | every droid is the sender's and alive; target exists and is *visible to the sender* (`visible[player]` — the order-through-fog cheat dies here); coords on map |
| `Build` | `NET_BUILD` (start), makes `NET_BUILDFINISHED`/`NET_DEMOLISH` server events | structure stat id, x,y, direction, builder droid ids | stat researched, `validLocation`, power cost, builder is theirs and can reach |
| `TemplateSet` / `TemplateDelete` | `NET_TEMPLATE`, `NET_TEMPLATEDEST` | template record / template id | every component researched by that player; body/prop/weapon combination legal (`checkValidWeaponForProp`); server assigns the template id |
| `Produce` | *(new — was implicit in owner-broadcast `NET_DROID`)* | factory id, template id, quantity delta / cancel, loop flag | factory theirs and built; template theirs; power reserved per unit started |
| `Research` | `NET_RESEARCHSTATUS` (start/stop), makes `NET_RESEARCH` a server event | facility id, topic id (or cancel) | facility theirs; topic available in their tree; not already done/underway; power |
| `StructureMode` | parts of `NET_SECONDARY` targeting factories, rally points | structure id, setting, value | theirs; setting applies to that structure type |
| `Embark` | `NET_DROIDEMBARK`, `NET_DROIDDISEMBARK` (as orders; the completed fact becomes entity state) | droid ids, transporter id / disembark x,y | capacity, ownership, adjacency |
| `LasSat` | `NET_LASSAT` | structure id, target x,y | it is theirs, charged, in range; the *effect* is the server's event |
| `Gift` | `NET_GIFT` (droids/power/tech), `modifyResources` | kind, recipient player, ids/amount | alliance exists (`alliances`), sender owns the goods, recipient can receive |
| `Alliance` | `NET_ALLIANCE` | offer/accept/break, player | game's alliance mode allows it; server updates the matrix and replicates it |
| `SessionControl` | *(new — solo `gameTimeSetMod` speed keys, the cheat console; pause existed here and is removed instead)* | verb (speed/cheat), argument | the session's flags permit it: a solo session allows both, a service session neither; operators authenticate separately (stage I) |
| `Resync` | `NET_REQUESTDROID`'s honest core | last good server tick | asks for a fresh keyframe of the interest set after datagram famine |

Messages that do **not** survive as commands, with the reason on record:

- `NET_DROID`, `NET_STRUCT`, `NET_WHOLEDROID` — "this object now exists" is
  the definition of client authority; existence is only ever a replication
  message now.
- `NET_DROIDDEST`, `NET_STRUCTDEST`, `NET_FEATUREDEST`, `NET_DESTROYXTRA`,
  `NET_VTOL`, `NET_VTOLREARM`, `NET_DEMOLISH`, `NET_BUILDFINISHED`,
  `NET_RESEARCH` — deaths, completions and rearms are simulation *outcomes*;
  they become replication below.
- `NET_CHECK_DROID`, `NET_CHECK_STRUCT`, `NET_CHECK_POWER` — drift correction
  between co-equal simulations; no co-equal simulations remain.
- `NET_SCORESUBMIT`, `NET_PLAYERSTATS` — clients no longer grade their own
  homework; the server computes scores (stage H persists them).
- `NET_ARTIFACTS` — random artifact placement was the host seeding peers; the
  server just places features and replicates them.
- The cheat console (`Cheat.cpp`) as a direct call into the simulation — it
  is a `SessionControl` verb a solo session permits and a service session
  refuses.

### Replication plane — the server's world, scoped per client

The unit of replication is the **interest set**: entities the connection's
player owns, plus entities `visible[player]` says they can see (stage G turns
this from optimisation into security). Per tick the server emits, per client:

| Message (S→C) | Channel | Replaces | Payload (sketch) |
|---|---|---|---|
| `Tick` | datagram, heads each snapshot | — | server tick `U32`, last command seq acked, game-speed factor |
| `Enter` | reliable | `NET_DROID`, `NET_WHOLEDROID`, `NET_STRUCT`, `NET_FEATURES`, join-in-progress set | kind, id, player, stat/template ref, x,y,z, direction, hp, kind-specific block (droid: order/action summary, weapons; structure: build %, functionality; feature: subtype) |
| `Update` | datagram, latest-wins | `NET_CHECK_DROID`'s honest half, `NET_DROIDMOVE` echoes | id, dirty mask, then only dirty fields: pos (quantised), direction, hp, order/action state, target id, ammo/rearm % |
| `Leave` | reliable | *(new — fog re-hiding)* | id — left the interest set, **not** dead; client keeps a ghost for fog memory exactly as the current structure-memory rendering does |
| `Destroy` | reliable | `NET_DROIDDEST`, `NET_STRUCTDEST`, `NET_FEATUREDEST`, `NET_DESTROYXTRA` | id, killer id (for effects/score UI), cause |
| `PlayerState` | reliable, own player only | `NET_CHECK_POWER` (was a broadcast of everyone's power — an information leak with a message type) | power `U32`, research in progress (facility→topic, %), production queues (factory→template, %, quantity), template list changes, kill/loss tallies |
| `ResearchDone` | reliable | `NET_RESEARCH` | player, topic — public completions (enemy tech becomes visible the way the game already announces it) |
| `Alliance` | reliable | `NET_ALLIANCE` state half | the updated alliance/colour matrices |
| `Effect` | datagram | *(new — today each sim invents its own effects)* | compact cosmetic events the replica cannot derive: weapon fired (attacker id, target id/pos, weapon stat), impact, structure hit flash, las-sat strike, transporter launch |
| `UiEvent` | reliable | the script calls that drive presentation from game logic: `scrAddMessage` (intelligence messages), `scrShowConsoleText`/`addConsoleMessage`, `scrCentreView`/`scrCentreViewPos` camera moves, `scrPlaySound`/`scrPlaySoundPos`, briefing/FMV sequence triggers | event kind + args; emitted by the server's script VM in place of the client calls it makes today — the campaign's presentation layer rides this row |
| `Scores` | reliable, periodic | `NET_SCORESUBMIT`, `NET_PLAYERSTATS` | per player: kills, losses, power earned — whatever the score screen shows |
| `GameState` | reliable | mission timer sync, `NET_FIREUP` follow-ups, the between-mission Continue flow | mission phase and timer, countdowns, game-speed state (server-decreed; in a solo session because its one client asked via `SessionControl`) |

Projectiles deliberately do **not** replicate as entities: at 10 Hz a bullet
is 2–3 datagram appearances of a thing the client can fly better itself.
`Effect { WeaponFired }` plus the authoritative `Update { hp }` / `Destroy`
outcomes reproduce today's visuals — which is exactly the split the current
code already makes between `proj_UpdateAll`'s visuals and the damage it
applies.

**Budgeting** replaces `okToSend()`'s global byte cap with per-connection,
per-entity priority: a fixed datagram budget per tick (initial: 1200 bytes ×
10 Hz ≈ 12 kB/s ceiling, far under QUIC comfort), filled in order of
(owned + in combat) > (visible + moving) > (visible + idle refresh), with
every entity guaranteed a refresh interval so a starved entity degrades to
today's 1000 ms `DROID_FREQUENCY` behaviour, not to absence. Envelope check:
8 players × ~100 visible droids × ~14-byte deltas at full dirty ≈ 11 kB per
snapshot *before* priority and dirty-masking — the scheme works with the
budget, and interest scoping keeps the common case a fraction of it.

### Sequence sketches

Qualifiers elided as in the tables; the arrow names the enum. Joining a game
in progress:

```
C→S  Hello {proto, buildHash}            S→C  Hello {ok, tickMs}
C→S  Auth {token}                        S→C  Auth {playerId, stats}
C→S  Join {gameId}                       S→C  LobbyState {options, slots}
C→S  MapRequest {mapHash?}               S→C  File {map...}   (if needed)
     (client loads level)                S→C  Start {startTick}
C→S  Ready                               S→C  Enter ×N  (interest set)
                                         S→C  PlayerState, Tick...
```

One order, validated:

```
C→S  Order {seq=41, droids=[1207,1210], DORDER_ATTACK, target=3311}
        server: both droids mine & alive? target visible[me]? → queue at tick T+1
S→C  Tick {tick=T+1, ack=41}
S→C  Update {1207: order=Attack, target=3311, dir...}      (and so on per tick)
S→C  Effect {WeaponFired 1207→3311} ... Update {3311: hp↓} ... Destroy {3311, killer=1207}
```

The same order with a fogged target instead earns
`CommandReject {seq=41, NotVisible}` and no world change — the receiving
peers in today's protocol would have just executed it.

---

## What moves where

The stage B audit will produce the real list; the expected split of
`Outpost/`'s 117 TUs, named so the size of the job is on record:

- **To `NeuronServer` (simulation):** Droid, Structure, Feature, Combat,
  Projectile, Move, FPath/AStar/GatewayRoute/OptimisePath, Action, Order,
  CmdDroid, Group, Formation, Power/PowerCrypt, Research, Function, Stats,
  AI/Action/ScriptAI + the script-binding tables, Visibility/AdvVis, Cluster,
  MapGrid, Map/Gateway, Mission logic, GTime consumers, ObjMem — plus new,
  named per [AGENTS.md §1](../AGENTS.md): `Session.cpp` (class `Session`,
  connections ↔ players ↔ session flags), `Commands.cpp` (the command
  validators), `Replication.cpp` (class `Replication`: interest sets, dirty
  masks, budgets).
- **Stays client-side (`Outpost/` + `NeuronClient`):** Display/Display3D,
  HCI and the widget screens, IntDisplay/IntOrder/IntelMap, Radar, Console,
  Effects/Atmos/Arrow/Bucket3D, Component rendering, WarCAM, input/KeyMap,
  SeqDisp/FMV, GameAudio, Text, FrontEnd/MultiInt/MultiMenu — plus new:
  `Replica.cpp` (class `Replica`, the id-keyed store feeding all of the
  above), `Predict.cpp` (the cosmetic acknowledgements).
- **Shared in `NeuronCore`:** Transport (widened with channels, plus
  `LoopbackTransport`), `NetWriter`/`NetReader` and the message definitions,
  GTime, script VM, Json, resources, StrRes.

Two known seams inside that list. **The script system serves both sides from
one VM**: the `.slo` corpus — mission logic, AI, triggers — runs on the
server, and the audit that matters is of the *instinct functions* it calls
([ScriptFuncs.cpp](../Outpost/ScriptFuncs.cpp) and its tables): the
world-mutating and querying majority bind server-side unchanged, while the
presentation-driving minority (`scrAddMessage`, `scrShowConsoleText`,
`scrCentreView`, `scrPlaySound`, sequence playback and kin) re-bind to
emitting `UiEvent` rather than calling client code. **`Visibility.cpp`
splits rather than moves**: the server needs it as the replication filter,
and the client keeps line-of-sight raycasts for presenting what it has been
sent.

## Security model, stated once

- **Authority:** clients cannot state facts, only wishes. Every command is
  validated for ownership, capability, cost, legality and *visibility* before
  it touches the world.
- **Information:** a client's process only ever contains what its player may
  know (stage G). The wire is TLS 1.3 end to end already (Phase 5).
- **Identity:** connections authenticate before joining (stage H); the build
  hash check remains as a compatibility gate, not a security measure — the
  design assumes a hostile client and stands anyway.
- **Abuse:** per-connection command rate limits with `RateLimited` rejects;
  the server logs command streams per session (the journal doubles as the
  bug-report replay [MigrationPlan.md](MigrationPlan.md)'s deleted NetTest
  never grew into).

## Decisions for the owner

1. **Adopt this direction and staging** (A–I as gates; D flips single player,
   F flips multiplayer).
2. ~~**Tick quantum**~~ — **answered by measurement, not open.** 40 ms / 25 Hz,
   because `BASE_DEF_RATE` says the movement system was written for it (see
   [the timing model](#the-timing-model)). What remains open is only whether a
   Windows run finds 25 Hz world motion visibly steppy before the renderer
   interpolates, which is a stage A verification item rather than a decision.
3. **Multiplayer's first rung**: stage F can ship with remote clients joining
   the host's *embedded* server (rung 1 — no second process to manage) or
   require the separate `OutpostServer.exe` (rung 3) from its first day.
4. **Zone model for stage I**: process-per-game on demand versus resident
   multi-zone processes — affects the directory protocol, nothing earlier.
5. **Whether this work takes a phase number** and where it sits against the
   open render work (Phase 8 D1b/D2) — it touches none of the same files.
6. **Player count per zone stays 8 for now**; widening `MAX_PLAYERS` is its
   own later project (array/bitfield audit), explicitly out of scope here.

Two decisions are already taken and recorded above rather than open: the
embedded-first topology with the separation ladder, and the removal of pause
(2026-08-27).
