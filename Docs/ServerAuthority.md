# Server Authority: the design for the MMO shape

This document is the design for taking *Outpost: Warzone* from its 1998
peer-distributed multiplayer model to a **server-authoritative MMO**: one
process that owns the truth about the world, and clients that send intents and
render what the server tells them. It records where the tree already stands,
why the current model cannot be the destination, the target architecture, the
staged plan, and — in the most detail — **the message protocol between client
and server**, derived message by message from the protocol the game speaks
today.

Like [AssetPipeline.md](AssetPipeline.md), this records a design without
claiming a phase number; whether it takes one is the owner's call. The
[decisions](#decisions-for-the-owner) it is gated on are listed at the end.

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

Three deployables, two of them new:

```
OutpostServer.exe  (new)      ── the authority ──────────────────────────────
  NeuronServer.lib            world simulation: droids, structures, combat,
  NeuronCore.lib              movement, pathfinding, power, research, AI,
                              scripts; validation of client commands;
                              per-player visibility as a replication filter;
                              fixed tick; headless (no window, D3D, audio)

Outpost.exe        (today's)  ── the view ───────────────────────────────────
  NeuronClient.lib            rendering, HCI, input, audio, FMV; a replica
  NeuronCore.lib              world updated from server messages; sends
                              intents, never state

Directory service  (new, later) ─ the front door ────────────────────────────
                              accounts and session tokens; the list of running
                              worlds/zones that answers FindSessions; player
                              stats and persistence storage
```

`NeuronCore` keeps what both sides speak: the transport, the wire protocol,
`GTime`, the script VM, resources, `Neuron::Json`. The dependency edges stay
exactly as [AGENTS.md §2](../AGENTS.md) draws them.

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
and all game arithmetic is in milliseconds, so the tick is chosen as a
millisecond quantum rather than a rate: **100 ms of game time per tick, 10
ticks per second** (decision 2 — the number is tunable; the *fixedness* is
not). `gameTimeUpdate()` grows a server variant that advances `gameTime` by
exactly the tick quantum, and the accumulator pattern lets a loaded server
catch up without the world slowing down.

The client's clock becomes derived: `serverTick × TickMs`, plus an
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
The order is chosen so that the riskiest structural work (A–C) happens while
the game still plays its current multiplayer, and the protocol flip (E) is one
switchable step rather than a long twilight of half-authority.

### A — Fix the timestep

Split `gameLoop()` into `gameSimulate()` — the block from
`eventProcessTriggers` through `objmemUpdate`: AI, movement, droids,
structures, projectiles, power, features, animation state — and the
render/UI remainder. Drive `gameSimulate()` from an accumulator at the fixed
tick; the renderer interpolates positions between the last two sim states (or,
as a first cut, renders the latest state as today).

*Why first:* a headless server needs a simulation entry point that is not a
frame handler, and identical tick maths on server and client makes replica
behaviour comparable. This also retires `GTIME_MAXFRAME` clamping as a source
of speed variation.

*Gate:* campaign and skirmish play at unchanged speed; `frameTime` no longer
reaches simulation code (grep gate on the moved block).

### B — Split simulation state from presentation state

The object model mixes the two: `DROID` carries screen coords, `sDisplay`,
IMD pointers and effect state beside hp, order and position. Introduce the
discipline (not yet the library move) file by file: simulation fields and
functions must not reach presentation fields, textures, `Screen.h` or audio.
The compiler enforces it once headers split — `DroidDef.h` into the sim record
plus a client-side visual record keyed by object id, and likewise structures,
features, projectiles.

*Gate:* `tools/crosscheck.py` builds a `SIM_ONLY` shadow configuration that
compiles the simulation TUs with `NeuronClient` headers absent. That
configuration is the future server's TU list, discovered rather than declared.

### C — `OutpostServer.exe`, headless

Create the server executable: `WinMain`-less console process linking
`NeuronServer` + `NeuronCore`, loading `GameData` through the existing
resource system and manifests, booting a map, running AI players, ticking.
The simulation TUs from stage B move out of `Outpost/` into `NeuronServer/`
(this is the move [AGENTS.md R13](../AGENTS.md) reserves for "a task that
*is* that split" — this is that task). `Outpost.exe` keeps compiling them via
project reference until stage E cuts its dependency.

*Gate:* the server boots a skirmish map with 8 AI players and runs it to a
win with no window, D3D, DirectInput or XAudio2 import in the binary. This is
also the moment CI regains a *runnable* test, which
[MigrationPlan.md](MigrationPlan.md) has wanted since NetTest was deleted:
a headless server that plays itself is a test that runs on the build agent.

### D — The protocol layer

Replace the `NETMSG` + `NetAdd`/`NetGet` memcpy idiom with an explicit,
versioned wire format (rules under [Wire format](#wire-format)) and widen the
`Transport` seam from one implicit channel to named channels (below). The
existing messages are re-expressed in it unchanged in meaning — this stage
changes *encoding*, not *authority*, so it is testable byte-for-byte against
a loopback session.

*Why now:* the current encoding memcpys structs with host padding and
pointer-width assumptions; every message added for authority would be built on
that. Doing encoding first also cleanly separates "the bytes changed" bugs
from "the authority changed" bugs.

### E — Flip authority

The pivot, kept as small as the preparation allows. In one coordinated change:

- Clients stop calling the simulation mutators and start sending the
  **command plane** messages below; `turnOffMultiMsg` and the
  apply-locally-then-broadcast idiom go.
- The server (initially: the hosting player's `OutpostServer`, spawned
  beside their client — a listen server in a separate process) validates and
  executes commands, and emits the **replication plane**.
- `MultiSync.cpp` is deleted whole: `NET_CHECK_DROID`, `NET_CHECK_STRUCT`,
  `NET_CHECK_POWER`, `SYNC_PANIC`, `okToSend` — an authority does not
  reconcile with its replicas. `whosResponsible`/`myResponsibility` go with
  it; AI players are simply server players.
- The client grows the **replica store**: objects created/updated/destroyed
  purely from replication messages, feeding the existing rendering and HCI.
  Object ids become server-minted; `(objID << 3) | player` survives only
  inside the server.

*Gate:* two clients and a server on loopback play a skirmish to completion;
a third client joins mid-game and gets a correct world from the snapshot
join path (which replaces the `NET_WHOLEDROID`/`NET_PLAYERCOMPLETE` dance).
Kill either client: the world continues; kill the server: both clients get
the session-over path.

### F — Interest management

Stop replicating what a player cannot see. The filter already exists and is
already maintained per tick: `visible[player]` on every object, driven by
`visibleObject`/`visTilesUpdate` and cheap lookups through the existing
`MapGrid`/`Cluster` systems. The replication layer sends a connection only
entities visible to (or owned by) its player, with enter/leave transitions
when visibility changes, and per-entity send priorities (owned and fighting >
visible and moving > static) replacing the old global byte budget.

This is the step that turns fog of war from a courtesy into **information
security**: after it, a maphack is impossible rather than impolite, because
the hidden half of the world never reaches the client's memory.

*Gate:* a packet-level assertion harness — a scripted client that verifies no
entity outside its vision set ever appears in its stream while a second
scripted client fights it.

### G — Identity and persistence

Accounts and tokens at connect (`C_AUTH` below); the local obfuscated stats
file (`NETmangleData`, `saveMultiStats`) becomes a server-side record keyed by
account; `NET_PLAYERSTATS`/`NET_SCORESUBMIT` invert — the server computes
scores and *tells* clients. The server checkpoints zone state on its own
schedule (the save-format problem died with the save system; the server's
checkpoint format is private to it and versioned like any server storage).

### H — The service

The directory: a small server the game client queries for "what worlds/games
exist" (finally answering `Transport::FindSessions` /
`NETfindGame`), for login, and for creating a game (which allocates or spins
up a zone server). Host migration stops being a concept — `HostLost` still
exists for the server dying, but the world can be revived from checkpoint.
Real certificates on the service replace `HostCertificate`'s self-signed one;
clients pin the directory and the directory vouches for zone servers.

Steps F–H are ordered by dependency, but E is the point of no return: from E
onwards the game *is* server-authoritative and everything after is widening
the service around it.

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
widens from `Send(reliable)`/`Broadcast` to
`Send(channel, bytes)` + `SendUnreliable(bytes)`; the game still never names
QUIC.

### Wire format

Rules, replacing the `NetAdd`/`NetGet` struct-memcpy idiom:

- Every field is written explicitly, little-endian, no padding — a
  `NetWriter`/`NetReader` pair in `NeuronCore` with width-explicit puts
  (`U8/U16/U32/S32/F32/String/Bits`). This also removes the last
  pointer-width and struct-layout hazards from the wire
  ([X64Readiness.md](X64Readiness.md) class).
- One-byte message type, then a protocol built for evolution: the
  `C_HELLO`/`S_HELLO` exchange carries a protocol version and the build hash
  (today's `NET_VERSION` + `NEThashVal` check); the server refuses mismatches
  with a reason string. No per-message version bytes.
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

| New message | Dir | Replaces | Payload (sketch) | Notes |
|---|---|---|---|---|
| `C_HELLO` | C→S | `NET_VERSION`, `NEThashVal` in `NET_OPTIONS` | protocol version, build hash | first bytes on the connection |
| `S_HELLO` | S→C | — | accept/refuse + reason, server tick rate, assigned connection id | |
| `C_AUTH` | C→S | *(new)* | account token from directory (stage G); before G: player name | |
| `S_AUTH` | S→C | *(new)* | account ok, persistent player id, rank/stats summary | replaces reading the local stats file |
| `C_GAME_LIST` | C→dir | `NETfindGame` → `Transport::FindSessions` | filters | the query Phase 5 left a seam for |
| `S_GAME_LIST` | dir→C | `GAMESTRUCT[]` fill of `NetPlay.games` | per game: name, address, players/max, the four `gameFlags` | keeps the browser working unchanged |
| `C_CREATE_GAME` | C→dir/S | `NEThostGame` | name, map, `MULTIPLAYERGAME` options | allocates a zone server (stage H); before H: starts the listen server |
| `C_JOIN` | C→S | `NETjoinGame` | game id, requested colour | |
| `S_LOBBY_STATE` | S→C | `NET_OPTIONS` (`sendOptions`) | `MULTIPLAYERGAME` (map, type, base, alliances, limits, power), slot table (player ↔ connection ↔ colour ↔ ready), structure limits | one authoritative lobby snapshot, re-broadcast on change; replaces the options + `player2dpid` + `JoiningInProgress` + colour + alliance memcpy train |
| `C_LOBBY_SET` | C→S | `NET_COLOURREQUEST`, options edits, `NET_TEMPLATE` (pre-game force) | field, value | host edits options; anyone edits own colour/ready/force |
| `S_PLAYER_JOINED` / `S_PLAYER_LEFT` | S→C | `Transport::Event` `PlayerJoined`/`PlayerLeft`, `NET_LEAVING` | player id, name, reason (left/kicked/timeout) | |
| `S_KICK` | S→C | `NET_KICK` | reason | server closes the connection after |
| `C_CHAT` / `S_CHAT` | both | `NET_TEXTMSG`, `NET_WHITEBOARD` | scope (all/allies/direct), text | server relays and filters by alliance; whiteboard drops unless wanted |
| `C_PING` / `S_PONG` | both | `NET_PING`, `PingTimes` | echo token | QUIC RTT stats supplement it for the bars UI |
| `C_MAP_REQUEST` | C→S | `NET_REQUESTMAP` | map hash I have (or none) | |
| `S_FILE` | S→C | `FILEMSG`, `NETsendFile`/`NETrecvFile` | own QUIC stream: name, size, bytes | |
| `S_START` | S→C | `NET_FIREUP` | start tick, countdown ms, map hash | clients load level, then: |
| `C_READY` | C→S | `NET_PLAYERRESPONDING` | — | server holds tick 0 until all ready or timeout |
| `S_GAME_OVER` | S→C | `NET_DMATCHWIN` (unused), local win detection, `multiplayerWinSequence` | outcome per player, final scores | win/lose becomes a server statement |

Join-in-progress needs no special plane: a joiner receives `S_LOBBY_STATE`,
then the replication plane's normal full-enter of every entity in its
interest set — the same path as fog clearing, so `NET_PLAYERCOMPLETE`,
`NET_REQUESTPLAYER`, `NET_STRUCT`, `NET_WHOLEDROID`, `NET_FEATURES` and
`bDisplayMultiJoiningStatus`'s progress dance all collapse into "replication
from an empty replica".

### Command plane — client intents

Every command carries a client **sequence number** (`U16`, wrapping) so the
server can ack, dedupe and rate-limit, and so a reject can name what it
rejects. The server answers only when the UI must know
(`S_CMD_REJECT { seq, reason }` — no power, not yours, not visible, illegal
placement, prereq missing, rate limited); acceptance is visible as world
change. **Anything the old protocol let a client *assert* is here demoted to
a request**, and the server performs the checks the receiving peers never
did:

| New message | Replaces | Payload (sketch) | Server validates |
|---|---|---|---|
| `C_ORDER` | `NET_DROIDINFO`, `NET_DROIDMOVE`, `NET_GROUPORDER`, `NET_SECONDARY`, `NET_SECONDARY_ALL`, `NET_REQUESTDROID` (obsolete) | droid id list (or group id), `DROID_ORDER`, x,y, target object id, secondary mask, queue/replace flag | every droid is the sender's and alive; target exists and is *visible to the sender* (`visible[player]` — the order-through-fog cheat dies here); coords on map |
| `C_BUILD` | `NET_BUILD` (start), makes `NET_BUILDFINISHED`/`NET_DEMOLISH` server events | structure stat id, x,y, direction, builder droid ids | stat researched, `validLocation`, power cost, builder is theirs and can reach |
| `C_TEMPLATE_SET` / `C_TEMPLATE_DEL` | `NET_TEMPLATE`, `NET_TEMPLATEDEST` | template record / template id | every component researched by that player; body/prop/weapon combination legal (`checkValidWeaponForProp`); server assigns the template id |
| `C_PRODUCE` | *(new — was implicit in owner-broadcast `NET_DROID`)* | factory id, template id, quantity delta / cancel, loop flag | factory theirs and built; template theirs; power reserved per unit started |
| `C_RESEARCH` | `NET_RESEARCHSTATUS` (start/stop), makes `NET_RESEARCH` a server event | facility id, topic id (or cancel) | facility theirs; topic available in their tree; not already done/underway; power |
| `C_STRUCT_MODE` | parts of `NET_SECONDARY` targeting factories, rally points | structure id, setting, value | theirs, setting applies to that structure type |
| `C_EMBARK` | `NET_DROIDEMBARK`, `NET_DROIDDISEMBARK` (as orders; the completed fact becomes entity state) | droid ids, transporter id / disembark x,y | capacity, ownership, adjacency |
| `C_LASSAT` | `NET_LASSAT` | structure id, target x,y | it is theirs, charged, in range; the *effect* is the server's projectile event |
| `C_GIFT` | `NET_GIFT` (droids/power/tech), `modifyResources` | kind, recipient player, ids/amount | alliance exists (`alliances`), sender owns the goods, recipient can receive |
| `C_ALLIANCE` | `NET_ALLIANCE` | offer/accept/break, player | game's alliance mode allows it; server updates the matrix and replicates it |
| `C_RESYNC` | `NET_REQUESTDROID`'s honest core | last good server tick | asks for a fresh keyframe of the interest set after datagram famine |

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
  homework; the server computes scores (stage G persists them).
- `NET_ARTIFACTS` — random artifact placement was the host seeding peers; the
  server just places features and replicates them.
- The cheat console (`Cheat.cpp`) — becomes server admin commands behind
  operator auth, never a client message.

### Replication plane — the server's world, scoped per client

The unit of replication is the **interest set**: entities the connection's
player owns, plus entities `visible[player]` says they can see (stage F turns
this from optimisation into security). Per tick the server emits, per client:

| Message | Channel | Replaces | Payload (sketch) |
|---|---|---|---|
| `S_TICK` | datagram, heads each snapshot | — | server tick `U32`, last command seq acked, game speed flags |
| `S_ENTER` | reliable | `NET_DROID`, `NET_WHOLEDROID`, `NET_STRUCT`, `NET_FEATURES`, join-in-progress set | kind, id, player, stat/template ref, x,y,z, direction, hp, kind-specific block (droid: order/action summary, weapons; structure: build %, functionality; feature: subtype) |
| `S_UPDATE` | datagram, latest-wins | `NET_CHECK_DROID`'s honest half, `NET_DROIDMOVE` echoes | id, dirty mask, then only dirty fields: pos (quantised), direction, hp, order/action state, target id, ammo/rearm % |
| `S_LEAVE` | reliable | *(new — fog re-hiding)* | id — left the interest set, **not** dead; client keeps a ghost for fog memory exactly as the current structure-memory rendering does |
| `S_DESTROY` | reliable | `NET_DROIDDEST`, `NET_STRUCTDEST`, `NET_FEATUREDEST`, `NET_DESTROYXTRA` | id, killer id (for effects/score UI), cause |
| `S_PLAYER_STATE` | reliable, own player only | `NET_CHECK_POWER` (was a broadcast of everyone's power — an information leak with a message type) | power `U32`, research in progress (facility→topic, %), production queues (factory→template, %, quantity), template list changes, kill/loss tallies |
| `S_RESEARCH_DONE` | reliable | `NET_RESEARCH` | player, topic — public completions (enemy tech becomes visible the way the game already announces it) |
| `S_ALLIANCE` | reliable | `NET_ALLIANCE` state half | the updated alliance/colour matrices |
| `S_FX` | datagram | *(new — today each sim invents its own effects)* | compact cosmetic events the replica cannot derive: weapon fired (attacker id, target id/pos, weapon stat), impact, structure hit flash, las-sat strike, transporter launch | 
| `S_SCORES` | reliable, periodic | `NET_SCORESUBMIT`, `NET_PLAYERSTATS` | per player: kills, losses, power earned — whatever the score screen shows |
| `S_GAME_STATE` | reliable | mission timer sync, `NET_FIREUP` follow-ups | mission timer value, countdowns, pause state (server-decreed only) |

Projectiles deliberately do **not** replicate as entities: at 10 Hz a bullet
is 2–3 datagram appearances of a thing the client can fly better itself.
`S_FX { weapon fired }` plus the authoritative `S_UPDATE { hp }` /
`S_DESTROY` outcomes reproduce today's visuals — which is exactly the split
the current code already makes between `proj_UpdateAll`'s visuals and the
damage it applies.

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

Joining a game in progress:

```
C→S  C_HELLO {proto, buildHash}          S→C  S_HELLO {ok, tickMs}
C→S  C_AUTH {token}                      S→C  S_AUTH {playerId, stats}
C→S  C_JOIN {gameId}                     S→C  S_LOBBY_STATE {options, slots}
C→S  C_MAP_REQUEST {mapHash?}            S→C  S_FILE {map...}   (if needed)
     (client loads level)                S→C  S_START {startTick}
C→S  C_READY                             S→C  S_ENTER ×N  (interest set)
                                         S→C  S_PLAYER_STATE, S_TICK...
```

One order, validated:

```
C→S  C_ORDER {seq=41, droids=[1207,1210], DORDER_ATTACK, target=3311}
        server: both droids mine & alive? target visible[me]? → queue at tick T+1
S→C  S_TICK {tick=T+1, ack=41}
S→C  S_UPDATE {1207: order=ATTACK target=3311 dir...}   (and so on per tick)
S→C  S_FX {weaponFired 1207→3311} ... S_UPDATE {3311: hp↓} ... S_DESTROY {3311, killer=1207}
```

The same order with a fogged target instead earns
`S_CMD_REJECT {seq=41, NotVisible}` and no world change — the receiving peers
in today's protocol would have just executed it.

---

## What moves where

The stage B/C audit will produce the real list; the expected split of
`Outpost/`'s 117 TUs, named so the size of the job is on record:

- **To `NeuronServer` (simulation):** Droid, Structure, Feature, Combat,
  Projectile, Move, FPath/AStar/GatewayRoute/OptimisePath, Action, Order,
  CmdDroid, Group, Formation, Power/PowerCrypt, Research, Function, Stats,
  AI/Action/ScriptAI + the script-binding tables, Visibility/AdvVis, Cluster,
  MapGrid, Map/Gateway, Mission logic, GTime consumers, ObjMem — plus new:
  `Session.cpp` (connections ↔ players), `Validate.cpp` (the command checks),
  `Replicate.cpp` (interest sets, dirty masks, budgets).
- **Stays client-side (`Outpost/` + `NeuronClient`):** Display/Display3D,
  HCI and the widget screens, IntDisplay/IntOrder/IntelMap, Radar, Console,
  Effects/Atmos/Arrow/Bucket3D, Component rendering, WarCAM, input/KeyMap,
  SeqDisp/FMV, GameAudio, Text, FrontEnd/MultiInt/MultiMenu — plus new:
  `Replica.cpp` (the id-keyed replica store feeding all of the above),
  `Predict.cpp` (the cosmetic acknowledgements).
- **Shared in `NeuronCore`:** Transport (widened with channels), the new
  `NetWriter`/`NetReader` and message definitions, GTime, script VM, Json,
  resources, StrRes.

Two known seams inside that list: the script system runs on **both** sides
(game-rule scripts and AI on the server; briefing/UI scripts on the client —
the `.slo` corpus needs tagging by side), and `Visibility.cpp` serves both
the server's replication filter and the client's local presentation of what
it has been sent (line-of-sight raycasts for its own UI). Both files split
rather than move.

## Security model, stated once

- **Authority:** clients cannot state facts, only wishes. Every command is
  validated for ownership, capability, cost, legality and *visibility* before
  it touches the world.
- **Information:** a client's process only ever contains what its player may
  know (stage F). The wire is TLS 1.3 end to end already (Phase 5).
- **Identity:** connections authenticate before joining (stage G); the build
  hash check remains as a compatibility gate, not a security measure — the
  design assumes a hostile client and stands anyway.
- **Abuse:** per-connection command rate limits with `RateLimited` rejects;
  the server logs command streams per session (the journal doubles as the
  bug-report replay [MigrationPlan.md](MigrationPlan.md)'s deleted NetTest
  never grew into).

## Decisions for the owner

1. **Adopt this direction and staging** (A–H as gates; E as the flip).
2. **Tick quantum**: 100 ms proposed; 50 ms doubles cost and halves latency.
3. **Listen-server interim** (stage E ships with the host running
   `OutpostServer` locally) versus jumping straight to hosted servers.
4. **Zone model for stage H**: process-per-game on demand versus resident
   multi-zone processes — affects the directory protocol, nothing earlier.
5. **Whether this work takes a phase number** and where it sits against the
   open render work (Phase 8 D1b/D2) — it touches none of the same files.
6. **Player count per zone stays 8 for now**; widening `MAX_PLAYERS` is its
   own later project (array/bitfield audit), explicitly out of scope here.
