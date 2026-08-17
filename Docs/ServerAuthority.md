# Server Authority: Survey and Staged Plan

This document answers one question: **what does it take to make this game
server-authoritative** — one process that owns the world, with clients that
render it and request changes to it, instead of today's model where every
machine owns a slice of the truth? It surveys how multiplayer actually
works in the shipped code, measures the distance from there to server
authority, and proposes a staged route. It is a design document in the
pattern of [AssetPipeline.md](AssetPipeline.md); no code changes accompany
it. Figures were measured on the tree at the head of this branch
(2026-08-17); defects noticed on the way are recorded in
[Appendix B](#appendix-b--defects-noticed-during-the-survey) and
deliberately not fixed here.

The direction itself is not this document's to decide — it is already on
the record three times: Phase 5 deleted LAN discovery unbuilt because "the
destination is a server-authoritative setup where a relay server owns the
connections" ([MigrationPlan.md](MigrationPlan.md), Phase 5); the save/load
removal was justified by "the game is heading to a server-authoritative
MMO shape" (MigrationPlan.md, 2026-08-16); and [AGENTS.md](../AGENTS.md)
§2 defines `NeuronServer` as the destination for "authoritative
simulation, session ownership" — a library that is still a PCH shell with
one translation unit. What has never been written down is what the
distance is and how to cross it. That is this document.

The short answer: **the road is shorter than it looks, because this game
was never lockstep.** The 1998 code runs a full simulation on every
machine and keeps them *approximately* aligned with a stream of
authoritative corrections — each machine authoritative for its own
player's objects. That is already state replication with distributed
authority. Server authority is not a rewrite of the model; it is
collapsing the set of authorities from N players to one, inverting one
message direction (orders become requests), and extracting the simulation
into a process with no screen. The extraction is the expensive part, and
it is the same work the `NeuronCore`/`NeuronClient`/`NeuronServer` split
already started.

---

## 1. How multiplayer works today

### 1.1 The topology is already a star

Phase 5's transport (`NeuronCore/Transport.cpp`) is a QUIC listener on the
host with one connection and one bidirectional stream per client.
**Clients never talk to each other**: "a client sending to another client
is relayed by the host" (`Transport.cpp:12-16`, `Transport.h:172`).
Reliable traffic is length-prefixed frames on the stream, ordered with
respect to each other; unreliable traffic is QUIC datagrams up to ~1,440
bytes, above which a send silently upgrades to reliable
(`Transport.cpp:532-545`). A broadcast never returns to its sender —
`NetPlay.cpp:222-225` records that the game *depends* on this, because a
sender applies its own broadcast locally as it sends.
`Transport::FindSessions` returns nothing unconditionally
(`Transport.cpp:1769-1779`), with a comment already naming its future: "a
relay server that owns the connections, where listing sessions is a
query" (`Transport.h:123`).

The transport host also does the only authority enforcement that exists
anywhere on the wire: a client cannot claim to be another player
(`Transport.cpp:948-949`). Everything above that line is trust.

So the *physical* shape of a server-authoritative system exists. What is
distributed is the *logical* authority that runs over it.

### 1.2 Authority is distributed by "responsibility"

`whosResponsible(player)` (`Outpost/MultiPlay.cpp:450-483`) is the whole
authority model: a human player is responsible for itself, and an AI
player belongs to the **lowest-numbered human at or below its index**,
else the highest-numbered human above it (`:465-477`). Everything follows
from the `myResponsibility(player)` checks threaded through the
multiplayer code (~30 sites): every machine simulates everyone, but only
*announces* state for the players it is responsible for, and accepts
announcements about everyone else. The skirmish AI scripts gate
themselves on the same test — twenty `if(not myResponsibility(player))
return` sites in `GameData/multiplay/script/skirmishAI.slo`, through
`scrMyResponsibility` (`ScriptFuncs.cpp:3304`).

The host is *not* special above the transport: it assigns slots and
colours, owns the lobby options broadcast, spawns the initial oil drums
and artifacts, and transfers the map file (`MultiJoin.cpp:275-306`,
`MultiOpt.cpp:75-115,851-852`, `MultiPlay.cpp:1287`) — but in the running
game it owns exactly its own units, like everyone else.

### 1.3 Synchronization is authoritative correction, not consensus

`Outpost/MultiSync.cpp` — "Magic happens here" (`:9`) — is the heart of
the model. Each machine periodically broadcasts **check messages** for
the objects it is responsible for: structures every 700 ms (one per
message, built ones only), droids every 1,000 ms (four per message, 30
bytes each: order, hit points, direction, position or movement floats,
target, kills — `packageCheck`, `:228-259`), power every 10,000 ms (5
bytes: the local player's absolute power), score every 25,000 ms, pings
every 12,000 ms (`:68-75`) — all budgeted by `okToSend` (`:81-91`)
against `game.bytesPerSec`/`game.packetsPerSec`, which for the only
selectable connection type is **1,201 bytes and 5 packets per second**
(`MultiPlay.h:186-200`, `MultiInt.cpp:452-453`) — a 1998 modem budget
still governing a QUIC link.

A receiver treats an incoming check as the truth and corrects its local
world: an on-screen droid takes only the hit-point correction (position
smoothing is commented out — `onscreenUpdate`, `:401-425`); an off-screen
droid, a VTOL, or any droid whose owner pings over 2,000 ms is
hard-snapped and re-routed when more than two tiles wrong
(`offscreenUpdate`, `:429-491`); order and kill-count divergence is
patched by issuing corrective orders locally with rebroadcast suppressed
(`highLevelDroidUpdate`, `:367-397`, under `turnOffMultiMsg`). The
structure check receiver goes further: an unknown structure is **built on
the spot**, a mismatched id is renamed, missing modules are retro-built
(`recvStructureCheck`, `:600-770`). And the model ships whole objects to
repair divergence: any handler that receives a message about a droid it
cannot find requests it (`sendRequestDroid`), and the responsible machine
answers with the complete object (`sendWholeDroid`,
`Multibot.cpp:416-842 → :977`). Mid-game object-level resync is not an
emergency mechanism; it is routine.

Two properties of this design matter enormously for what follows:

- **The game already tolerates authoritative correction.** Every client
  has, since 1998, accepted "here is where your view of this object is
  wrong, fix it" messages and repaired its world mid-frame — up to and
  including constructing buildings it never saw started. A client under
  server authority does exactly this. The only change is who sends.
- **The game has never been deterministic, and nothing depends on it
  being so.** There is no synchronized random seed — no `srand` at game
  start anywhere, and the one script-reachable seeding call uses
  `clock()` (`ScriptFuncs.cpp:3427`). `rand()` decides to-hit rolls, miss
  scatter, morale breaks and fire timing (~210 sites; `Combat.cpp:132`
  even gates firing on `rand()` against absolute `gameTime`). The
  simulation advances by wall-clock frame delta — `gameTimeUpdate`
  (`NeuronCore/GTime.cpp:103`) reads `GetTickCount`, clamps a slow frame
  to 166 ms (`:153-173`), and movement integrates through a ten-frame
  rolling average of `frameTime` (`Move.cpp:273-291`) in floats. Two
  machines' `gameTime` values are unrelated (each starts its own at 2)
  and no timestamp ever crosses the wire. The usual hard road to server
  authority — first make the sim deterministic, then argue about whose
  result wins — does not apply here, because the sync model never needed
  determinism. Machines *diverge constantly* and the check stream reels
  them back.

### 1.4 Commands are announcements, not requests

When the local player orders a droid, the order is applied locally and
broadcast — `orderDroid` applies first and then sends
(`Order.cpp:1679-1682`), `orderDroidLoc` sends first and applies with
rebroadcast suppressed (`:1706-1719`), multi-selection goes as one
`NET_GROUPORDER` (`Order.cpp:2183-2186` → `Multibot.cpp:647`). The same
apply-and-announce shape holds across the whole surface: build started,
build finished, demolish, destruction, research status, research
completed, secondary orders, embark, gifts (§1.6 and Appendix A). **There
is no request-to-host path for any gameplay action** — the only
host-arbitrated messages are lobby ones (colour requests, map transfer).
Nothing anywhere validates a received command against the rules: a
machine that says "this droid is destroyed" or "my research completed" is
believed outright.

Power is the purest case: every machine accrues every player's power
locally each frame (`Loop.cpp:248-249`), spends it locally, and every ten
seconds each machine broadcasts its own player's absolute total, which
receivers write over their copy unconditionally (`recvPowerCheck` →
`setPower`, `MultiSync.cpp:800-809`). AI players' power is never checked
by anyone — the sender always reports `selectedPlayer` (`:791`).

The clearest symptom of what this trust costs is
`Outpost/PowerCrypt.cpp`: 124 lines keeping an XOR-scrambled shadow copy
of each player's power so that local memory tampering can be *detected*
and a cheat message broadcast. In a client-authoritative world, anti-cheat
can only ever be a tripwire on one machine's honesty. Under server
authority the whole file class becomes meaningless — power lives on a
machine the player cannot touch.

### 1.5 The simulation is interleaved with the screen

`gameLoop` (`Outpost/Loop.cpp:136`) is one function that flips the
screen, updates audio, runs the widget UI, pumps the network
(`multiPlayerLoop`, `:244-245` — send checks, then drain and apply
everything inbound, `MultiPlay.cpp:206-253`), and then walks every droid,
structure, mission list, projectile and feature (`droidUpdate :270`,
`structureUpdate :346`, `proj_UpdateAll :384`), finishing with object
garbage collection (`objmemUpdate :419`). `WinMain.cpp:292,359` drives it
once per rendered frame and advances `gameTime` afterwards. There is no
seam where "the world updates" is separable from "the frame renders" —
that seam is the main thing the plan below has to build.

The entanglement is real but bounded, and it was measured (grep over the
core simulation files for audio, effect and UI/console calls):

| File | audio | effects | UI/console |
|---|---:|---:|---:|
| `Droid.cpp` | 11 | 5 | 13 |
| `Structure.cpp` | 7 | 23 | 24 |
| `Projectile.cpp` | 6 | 31 | 0 |
| `Mission.cpp` | 13 | 9 | 50 |
| `Order.cpp` | 3 | 2 | 3 |
| `Move.cpp` | 2 | 1 | 1 |
| `Combat.cpp`, `Function.cpp` | 0 | 0 | 0 |

Roughly 250 call sites where simulation reaches for the presentation
(plus `ScriptFuncs.cpp`'s 43 — briefings, console text, camera moves
driven by mission scripts). The full file classification is the gap
table's backing data (§3). The repository already owns the pattern that
removes them: Phase 9's `AudioWorld` (`NeuronClient/AudioSystem.h:19`) —
a provider struct the game fills in, so the engine names no game symbol.
The same inversion pointed the other way — the sim raises typed events,
the client's presentation subscribes — is what stage B below does. Two
smaller precedents already lean this direction: `GTime`'s frame counter
moved into `NeuronCore` precisely because "a dedicated server has a frame
number without having a window" (`GTime.cpp:327-337`), and the current
seam between "the simulation acting" and "the network replaying" is a
single global toggle, `turnOffMultiMsg` (`MultiPlay.cpp:112-135`), used
at 20+ sites and explicitly unable to nest.

### 1.6 The message surface

The protocol is 48 sequential message types (`MESSAGE_TYPES`,
`Outpost/MultiPlay.h:13-120`) plus an out-of-band file-transfer type, in
an 8 KB-bodied envelope (`NETMSG`, `NeuronCore/NetPlay.h:36-42`),
dispatched by two switches in `recvMessage` (`MultiPlay.cpp:583-783`)
plus a lobby switch (`frontendMultiMessages`, `MultiInt.cpp:1830-1928`).
By role:

- **Commands and events** (apply-and-announce): droid create/order/move/
  secondary/embark/destroy, group orders, build started/finished/
  demolish/destroy, feature destroy, research status/completed, templates,
  LasSat fire, VTOL state, gifts, alliances, artifacts, text chat.
- **The check stream** (periodic authoritative correction): droid,
  structure, power, score, ping.
- **Repair**: request-droid / whole-droid (unicast).
- **Lobby and session**: version check, options broadcast, colour
  request (the one host-arbitrated game message), kick, fire-up, leaving,
  player-responding, player stats, map request + file transfer.
- **Dead**: six types survive only in comment blocks, one
  (`NET_VTOLREARM`) is built but never transmitted (Appendix B).

The full table — every live type, where it is sent, its reliability, and
where it is handled — is [Appendix A](#appendix-a--the-message-table).
Two structural facts matter for the plan: **only the check stream is
bandwidth-budgeted** (`okToSend` gates the five periodic senders and
nothing else), and reliability is per-type and mostly *un*reliable —
orders included — because the check stream mops up losses.

### 1.7 What exists on the server side already

`NeuronServer` is a PCH shell — one translation unit including
`NeuronCore.h`. But the platform under it is readier than that suggests:

- **`NeuronCore` is already screen-free**: resource system,
  `Neuron::Json`, script compiler and VM, `GTime`, the QUIC transport and
  the `NET*` session layer all link without Direct3D, DirectInput or
  XAudio2. The 2026-08-16 split moved presentation, input and audio out
  to `NeuronClient` precisely so a server build would not drag them in.
- **The data is server-readable**: every manifest, stats table and config
  is JSON read by `Neuron::Json` — one reason the asset-pipeline work
  called itself "arguably prerequisite" for the server build-out
  (AssetPipeline.md §8).
- **x64 compiles and links** in both configurations
  ([X64Readiness.md](X64Readiness.md)) — the natural home of a long-lived
  server process — though nothing has run it.
- **The verification pattern exists**: `NetTest/` (deleted with the
  restructure) was a CI harness that started two real processes and put
  real bytes between them. [Verification.md](Verification.md) names that
  as the one property worth keeping. The server work brings it back.

---

## 2. What "server-authoritative" means here, concretely

One process — the **game server** — runs the only simulation that counts.
It owns every object, every player's power, research and score, the
mission timer, the AI scripts, the random rolls, and the allocation of
object ids (today a per-machine counter whose collisions the protocol
works around by shipping chosen ids — `ObjMem.cpp:248-249`,
`Multibot.cpp:524`). Clients:

1. send **requests**: "order these droids", "build this here", "start
   this research" — the same payloads `Multibot.cpp` and friends already
   serialize, sent to the server instead of broadcast, and **not applied
   locally**;
2. receive the **authority stream**: the same check, creation,
   completion and destruction messages the protocol already defines, now
   emitted by one sender for all players — including a request's own
   echo, which is when the requesting client applies it;
3. **keep running their local simulation, exactly as today.** This is
   the design's load-bearing choice: the client sim stops being an
   authority and becomes the prediction layer — the thing that moves
   units smoothly between authoritative corrections — which is precisely
   the role the check stream already forces it to play for every *other*
   player's units today. A thin client that only renders received state
   would need interpolation machinery this game has never had; a
   predicted client needs nothing new. An RTS absorbs the order
   round-trip in ways a shooter cannot — the unit's acknowledgement bark
   is presentation and stays local and instant.

The server validates requests instead of trusting announcements:
ownership on everything, power and prerequisites on build and research,
placement on build — the same checks the UI already runs before enabling
a button, run again where the player cannot reach them. A client that
sends impossible things is ignored and logged, not believed.
`PowerCrypt.cpp` and the cheat-broadcast machinery retire.

Single-player and the campaign run **the same server simulation
in-process** — the sim library ticked from the game loop, no socket, no
second process. One simulation codebase, two homes. Keeping a separate
offline sim path would fork the simulation permanently, and is rejected
for the same reason the manifest work refused two loaders for one format.

What this is *not*, at least not in this plan: deterministic lockstep
(nothing needs it — §1.3); client-side prediction *of commands* with
rollback (the RTS latency budget makes it polish, not foundation);
sharding or interest management at MMO scale. The MMO-shaped world the
owner decisions point at sits *around* this server — directory, accounts,
persistence, many matches per host — and §4's lane keeps it open without
building any of it.

---

## 3. The gap, measured

| # | Gap | Where it lives | Size |
|---|---|---|---|
| 1 | Orders apply locally before broadcast | the command surface of §1.6 | one redirect per sender, one echo rule on the server |
| 2 | Authority is per-player | `whosResponsible` + ~30 `myResponsibility` sites + 20 script gates | small — the function's answer becomes "the authority" |
| 3 | Checks are sent by everyone, within a modem budget | `MultiSync.cpp` send half | becomes authority-only; rates become data |
| 4 | Sim and presentation share one loop and one set of files | `Loop.cpp` + ~250 call sites (§1.5) | **the bulk of the work** |
| 5 | No headless process exists | `NeuronServer` shell | new, but small once 4 is done |
| 6 | Nothing validates commands | nowhere | new code on the authority's receive path |
| 7 | CI cannot run any of it | `NetTest` deleted | resurrect the two-process harness |

Gap 4 dominates, and the survey's file classification says where it
lives. Of the 117 `Outpost/` translation units, measured by density of
presentation references: **~50 are already presentation-free** — the
pathfinding stack, combat, mechanics, map, objects, stats, groups,
formations, the multiplayer protocol files themselves; **~30 have one to
six leaked call sites** — `Move.cpp`, `Action.cpp`, `Order.cpp`,
`Research.cpp`, `Power.cpp`, `Feature.cpp` among them; and the entangled
middle is small and known: **`Droid.cpp`, `Structure.cpp`,
`Projectile.cpp`, `Mission.cpp`** hold the authoritative object updates
with effects and audio inline, plus `ScriptFuncs.cpp` where mission
scripts drive briefings and camera. The pure-presentation files (`HCI`,
`Design`, `Display3D`, `IntDisplay`, the `Int*`/frontend family) never
move and never mattered.

---

## 4. Staged plan

Four stages plus a lane. Each is independently shippable and
CI-checkable, in the migration's standing pattern; A and B are also
independently *abandonable* — if the direction changed after either, the
tree would still be better than before it. Estimates are deliberately
absent: this repository records measured figures, and the honest
statement before stage B is scoped file-by-file is "gap 4 dominates".

### Stage A — Host authority, inside the existing game

The smallest change that makes one machine own the truth. No new
process, no file moves, multiplayer only (`bMultiPlayer` already gates
all of it).

- `whosResponsible(player)` answers "the host" for every player. With
  that one collapse: only the host sends checks, destruction, creation
  and completion; the skirmish AI issues commands only on the host (its
  script gates read the same function); clients' announcements stop.
- The command surface inverts. A non-host client sends its order to the
  host and **does not apply it locally**; the host validates, applies,
  and re-broadcasts the same message — and because a broadcast never
  returns to its sender, the host's broadcast reaches the requester,
  whose existing `recv*` handler applies it exactly as it applies any
  remote player's order today. The `turnOffMultiMsg` wrapping already
  present in every handler suppresses re-echo. Client prediction of
  everyone's movement continues untouched (§2.3).
- The host validates: ownership on every command; power and
  prerequisites on build and research; placement on build. The functions
  exist — they are what the UI calls to enable buttons.
- The power model becomes honest: the authority announces every player's
  power, not just its own (today AI power is never synced at all —
  §1.4), and clients stop accruing authoritatively.
- The check rates and the `okToSend` budget become data. The shipped
  1,201 bytes/sec was a modem's; QUIC on a modern link carries three
  orders of magnitude more. Defaults stay shipped values until a real
  run measures new ones — behaviour-preserving first, tuning after.

What stage A buys: the authority semantics, the validation code, the
request/echo round-trip and its feel are all proven inside the running
game before any extraction risk is taken. The known cost: the host
player's own commands stay instant — a fairness skew accepted here,
dissolved in stage C when the authority stops being a player's machine.

*Verified by:* two Windows instances on one machine (the standing
multiplayer smoke test), plus a packet-level assertion that after game
start no client ever sends a check or destruction message.

### Stage B — The seam: a world that can tick without a screen

The bulk of the work, and it is the server half of the split AGENTS.md
reserves. Three sub-stages, each cross-checkable:

- **B1 — presentation out of the sim.** The ~250 audio/effect/UI call
  sites in simulation files route through one provider struct — the
  `AudioWorld` inversion pointed the other way: the sim raises typed
  events ("structure completed", "projectile impacted at x,y,z",
  "research done", "console text N"), and the game exe wires them to
  today's audio, effects and console calls. `ScriptFuncs.cpp`'s
  script-driven briefing and camera effects join the same surface.
  Behaviour-preserving by construction; each file convertible in its own
  commit; also the moment the sim's `rand()` stops being shared with
  particle effects (Appendix B notes why that matters less than it
  looks, but hygiene is hygiene).
- **B2 — the tick extracted.** The world-update block of `gameLoop`
  (§1.5's list, in its exact order) moves into one function —
  `simTick(dt)` — that `gameLoop` calls where the block used to be, same
  `dt`, same order. The campaign and single-player run on it from this
  moment: one simulation path, no fork.
- **B3 — the headless gate.** A minimal console target that links
  `simTick` and its data loading *without* `NeuronClient`, built by CI
  on every push. Like the save/load deletion's "deleted to a fixpoint",
  this forces the dependency cleanup to done and then holds it there: a
  presentation symbol leaking back into the sim becomes a link error,
  not a review comment.

Where the sim files land is decision 1 (§5); the moves themselves happen
only after B3's gate holds, when they are mechanical project-file work.

*Verified by:* `crosscheck.py` per commit; the B3 link gate in CI; the
campaign boot run for B2 (`-game CAM_1A` plus a tutorial visit, since the
tutorial leans on `gameTime2` pause semantics — `Loop.cpp:174-180`).

### Stage C — The dedicated server process

- A headless server executable: loads manifests, stats and the map
  through the paths B3 proved, hosts the Transport listener, runs the
  lobby protocol (the session subset of Appendix A, without the UI that
  drives it today), ticks `simTick` on a fixed clock, emits the
  authority stream, and validates requests with stage A's rules — now on
  a machine no player owns.
- "Host Game" in the client spawns a local server process and joins it —
  the listen-server experience on the dedicated-server architecture. A
  typed address joins a remote one, exactly as today.
- Initial state needs no snapshot: as today, every machine constructs
  the start world from the map file and the options broadcast
  (`campInit`), and the authority stream corrects from tick one. A
  mid-game **reconnect** does need the world shipped — the
  `sendWholeDroid` pattern generalized to a full snapshot — and since
  mid-game join is not implemented today (its code is a comment block,
  `MultiJoin.cpp:347-581`), that is new capability, deliberately in the
  lane rather than on stage C's critical path.
- The authority stream gains a server tick stamp. Today no timestamp
  crosses the wire and no two clocks are related (§1.3); one `u32` tick
  in the check envelope gives clients a drift measure and the lane a
  foundation.
- The scripts run on the server only — mission logic and skirmish AI
  issue commands where the authority is; their presentation effects are
  already events after B1.
- CI runs again: the `NetTest` shape reborn — the server process plus a
  scripted headless client that connects, joins, fires up, issues
  orders, and asserts the authority stream answers (the client is B3's
  console target plus the `NET*` layer it already links).

*Verified by:* the CI harness above on every push; on Windows, the
listen-server flow end-to-end and a two-box game against the dedicated
exe.

### Stage D — Retire the old trust model

Only after C carries the load: delete `PowerCrypt.cpp` and the
cheat-broadcast machinery; delete distributed responsibility
(`whosResponsible` and every consumer, script hook included); delete the
client send-half of `MultiSync.cpp`, the `turnOffMultiMsg` toggle (the
client no longer applies anything that needs rebroadcast suppression),
and the request-droid/whole-droid repair loop; finish command validation
(placement, rate caps, alliance and gift rules); fix the file-transfer
path to write only server-named files into a designated directory
(Appendix B). Optionally — the anti-maphack line, listed rather than
promised — per-player visibility filtering of the authority stream.

### The lane beyond (not this plan)

The MMO-shaped world sits *around* the stage C server, not inside it:
`Transport::FindSessions` answered by a directory service; account
identity replacing the per-session self-signed certificate; server-side
persistence — the successor to the deleted save/load, living where the
authority lives; reconnect via the world snapshot; one host process
running many matches; interest management when a world stops fitting one
stream. Every one of these consumes the stage C server as-is; none
changes its shape. Recorded so no stage above forecloses them, and
deliberately unplanned.

---

## 5. Decisions needed

1. **Where the authoritative sim lives.** AGENTS.md §2 assigns
   "authoritative simulation, session ownership" to `NeuronServer`; but
   the sim is *game* code (droids, structures, research), and the
   repository's layering keeps game code out of engine libraries. Either
   (a) `NeuronServer` takes it, reading AGENTS.md literally, or (b)
   `NeuronServer` takes session/tick/replication scaffolding and a new
   game-side static library takes the sim, linked by both executables.
   (b) preserves the layering and is recommended; either way the moves
   wait for B3's gate.
2. **Stage A: ship it or fold it into C.** It is a temporary mode — C
   replaces host-as-authority with a process — but it is the only way to
   prove the request/echo semantics and the validation rules inside the
   running game before the extraction. Recommended: ship it.
3. **The server tick.** `simTick(dt)` keeps the variable-`dt` code paths
   (they are load-bearing everywhere), and the server calls it on a
   fixed clock. Recommended start: 30 Hz tick, droid checks at 10 Hz,
   structure checks at 2 Hz, power at 1 Hz — all held as data beside the
   old modem budget, none trusted until a run measures them.
4. **The campaign moves onto `simTick` in B2.** One simulation path is
   the point; the risk is the campaign's pause states and
   `gameTime2`-driven tutorial triggers. Recommended: yes, with the
   campaign boot and tutorial on B2's run checklist.
5. **Reconnect scope.** The world snapshot (and therefore mid-game
   rejoin) is lane work by this plan. If the owner wants reconnect in
   the first shipping server, it moves onto stage C and brings a
   versioned wire serialization of world state with it — sized by what
   `sendWholeDroid` already ships per object, and designed fresh rather
   than resurrected from the deleted save format.
6. **Server platform.** The server exe targets Win32 first — the only
   configuration that has ever run — and moves to x64 when
   [X64Readiness.md](X64Readiness.md)'s run gate passes. The
   long-lived-process argument for x64 is real but not urgent at
   one-match scale.

---

## Appendix A — the message table

The live protocol, from the survey (dead types listed at the end).
"Rel" is the reliability flag at the send site. Handlers are in
`recvMessage` (`MultiPlay.cpp:583-783`) unless noted `[lobby]`
(`frontendMultiMessages`, `MultiInt.cpp:1830-1928`).

| Type | Sent from | Rel | Handler |
|---|---|---|---|
| `NET_DROID` | `Multibot.cpp:535` (droid built) | U | `recvDroid :542` |
| `NET_DROIDINFO` | `Multibot.cpp:824` (single order) | U | `recvDroidInfo :830` |
| `NET_DROIDDEST` | `Multibot.cpp:948` | R | `recvDestroyDroid :955` |
| `NET_DROIDMOVE` | `Multibot.cpp:473` | U | `recvDroidMove :480` |
| `NET_GROUPORDER` | `Multibot.cpp:686,730` | U | `recvGroupOrder :737` |
| `NET_TEMPLATE` | `MultiPlay.cpp:1065` | U | `recvTemplate :1070` |
| `NET_TEMPLATEDEST` | `MultiPlay.cpp:1140` | U | `recvDestroyTemplate :1146` |
| `NET_FEATUREDEST` | `MultiPlay.cpp:1185` | R | `recvDestroyFeature :1191` |
| `NET_PING` | `MultiSync.cpp:974,991` | U | `recvPing :982` |
| `NET_CHECK_DROID` | `MultiSync.cpp:220` | U | `recvDroidCheck :263` |
| `NET_CHECK_STRUCT` | `MultiSync.cpp:572` | U | `recvStructureCheck :600` |
| `NET_CHECK_POWER` | `MultiSync.cpp:796` | U | `recvPowerCheck :800` |
| `NET_VERSION` | `MultiJoin.cpp:84` | R | `recvVersionCheck :89` |
| `NET_BUILD` | `MultiStruct.cpp:94` | U | `recvBuildStarted :100` |
| `NET_STRUCTDEST` | `MultiStruct.cpp:290` | U | `recvDestroyStructure :296` |
| `NET_BUILDFINISHED` | `MultiStruct.cpp:169` | U | `recvBuildFinished :174` |
| `NET_RESEARCH` | `MultiPlay.cpp:798` | U | `recvResearch :825` |
| `NET_TEXTMSG` | `MultiPlay.cpp:992` | U | `recvTextMessage :1023`, also [lobby] |
| `NET_LEAVING` | `MultiOpt.cpp:435` | R | inline `:739`, also [lobby] |
| `NET_REQUESTDROID` | `Multibot.cpp:1224` | U | `recvRequestDroid :1232` |
| `NET_WHOLEDROID` | `Multibot.cpp:1079` (unicast) | U | `receiveWholeDroid :1086` |
| `NET_PLAYERRESPONDING` | `MultiOpt.cpp:878` | R | inline `:752`, also [lobby] |
| `NET_OPTIONS` | `MultiOpt.cpp:113` | R | `recvOptions :137`, also [lobby] |
| `NET_KICK` | `MultiInt.cpp:1179` | R | inline `:765`, also [lobby] |
| `NET_SECONDARY` | `Multibot.cpp:200` | U | `recvDroidSecondary :205` |
| `NET_FIREUP` | `MultiInt.cpp:1169` | R | [lobby] `:1892` |
| `NET_ALLIANCE` | `MultiGifts.cpp:446` | R | `recvAlliance :459`, also [lobby] |
| `NET_GIFT` | `MultiGifts.cpp:166,212,277,309`; `Structure.cpp:6594` | R | `recvGift :72` |
| `NET_DEMOLISH` | `MultiStruct.cpp:255` | U | `recvDemolishFinished :259` |
| `NET_COLOURREQUEST` | `MultiInt.cpp:1019` | R | `recvColourRequest :1030` (host-only) |
| `NET_ARTIFACTS` | `MultiGifts.cpp:536,607,664` | U | `recvMultiPlayerRandomArtifacts :718` |
| `NET_SCORESUBMIT` | `MultiSync.cpp:866` | U | `recvScoreSubmission :880` |
| `NET_DESTROYXTRA` | `MultiPlay.cpp:1233` | U | `recvDestroyExtra :1240` |
| `NET_VTOL` | `Multibot.cpp:85` | U | `recvHappyVtol :90` |
| `NET_VTOLREARM` | built `Multibot.cpp:131`, **never sent** | — | `recvVtolRearm :136` (unreachable) |
| `NET_WHITEBOARD` | `MultiInt.cpp:2653` | U | [lobby] `:1860` |
| `NET_SECONDARY_ALL` | `Multibot.cpp:235` | U | `recvDroidSecondaryAll :239` |
| `NET_DROIDEMBARK` | `Multibot.cpp:265` | U | `recvDroidEmbark :270` |
| `NET_DROIDDISEMBARK` | `Multibot.cpp:304` | U | `recvDroidDisEmbark :309` |
| `NET_RESEARCHSTATUS` | `MultiPlay.cpp:890` | U | `recvResearchStatus :895` |
| `NET_LASSAT` | `MultiStruct.cpp:330` | U | `recvLasSat :336` |
| `NET_REQUESTMAP` | `MultiOpt.cpp:221` | R | [lobby] `:1841` |
| `NET_PLAYERSTATS` | `MultiStat.cpp:581` | R | `recvMultiStats :588` |
| `FILEMSG` (254) | `NetPlay.cpp:322` | R | `NETrecvFile` via [lobby] `:1844` |

Dead types (comment blocks only): `NET_PLAYERCOMPLETE`,
`NET_REQUESTPLAYER`, `NET_STRUCT`, `NET_FEATURES`, `NET_DMATCHWIN`, and
the removed `NET_WAYPOINT`.

## Appendix B — defects noticed during the survey

Recorded, not fixed — this is a design document.

- **`sendVtolRearm` never transmits.** It builds the message and returns
  without calling `NETbcast`/`NETsend` (`Multibot.cpp:111-133`), so VTOL
  rearm state has silently never been synchronized; `recvVtolRearm` is
  unreachable.
- **`NETrecvFile` writes whatever path the packet names.** The filename
  comes from the message body and goes straight to `fopen(..., "wb")`
  with no path validation and no failure check on the handle
  (`NetPlay.cpp:337-364`); a hostile host can write outside the game
  directory, and a dropped first chunk crashes on a null `FILE*`. Stage
  D's file-transfer hardening owns this.
- **`packageCheck` dereferences a null target.** When a checked droid's
  order is `DORDER_ATTACK`, `pD->psTarget->id` is read without a null
  check (`MultiSync.cpp:249-250`).
- **A batch dies on its first unknown object.** `recvDroidCheck`
  (`MultiSync.cpp:310-314`) and `recvGroupOrder` (`Multibot.cpp:773-777`)
  abandon the remainder of a multi-record message when one id fails to
  resolve, dropping valid records that follow.
- **`NET_FIREUP` falls through into `NET_KICK`** in the lobby switch when
  `ingame.localOptionsReceived` is false (`MultiInt.cpp:1892-1913` — no
  `break` outside the `if`).
- **The netplay log's name table is stale**: type 27 prints
  `NET_WAYPOINT` but 27 is `NET_KICK` (`NetSupp.cpp:128`).
- **AI power is never synchronized** — `sendPowerCheck` always reports
  `selectedPlayer` (`MultiSync.cpp:791`), so AI players' power drifts
  apart on every machine for the whole game.
- **`SYNC_PANIC` is dead**: declared at `MultiSync.cpp:75`, referenced
  only in commented code (`:335`), so the "dirty fix after 40 s" it
  promises never happens.
