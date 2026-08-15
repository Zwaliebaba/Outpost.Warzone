# Phase 5 — Networking: custom UDP transport on WinSock2

Working plan for the phase described in [MigrationPlan.md](MigrationPlan.md#phase-5--networking-custom-udp-transport-on-winsock2).
As with Phase 4, the figures were measured against the tree rather than
estimated.

**This is the largest phase in the plan**, larger than Phase 4 and not
comparable to Phase 3. It is also the first one whose result cannot be checked
here at all — see [Verification](#verification-is-the-problem), which is the
part of this document worth reading first.

## Current state

| File | Lines | DirectPlay sites | Fate |
|---|---:|---:|---|
| `Outpost/MPDPXtra.cpp` + `.h` | 1401 | 17 | delete (Mplayer.com, dead since 2001) |
| `NeuronCore/NetPlay.cpp` | 530 | 32 | rewrite — init, send, recv, stats |
| `NeuronCore/NetAudio.cpp` | 454 | 0 | **delete** — voice chat, dropped not ported |
| `NeuronCore/NetJoin.cpp` | 401 | 42 | rewrite — host, join, find, close, game flags |
| `NeuronCore/NetProv.cpp` | 269 | 17 | mostly deleted — IPX, serial and modem addresses |
| `NeuronCore/NetSupp.cpp` | 268 | 30 | rewrite — logging and support |
| `NeuronCore/NetCrypt.cpp` | 265 | **0** | **untouched** — sits above the transport |
| `NeuronCore/NetUsers.cpp` | 257 | 29 | rewrite — players, names, player data |
| `NeuronCore/NetLobby.cpp` | 245 | 38 | delete — DirectPlay lobby launch |
| `NeuronCore/NetPlay.h` | 217 | — | the interface; embeds DirectPlay types |
| `Outpost/MPlayer.cpp` | 83 | 2 | delete (with MPDPXtra) |

MigrationPlan.md's Phase 5 lists `NetPlay`, `NetSupp`, `NetLobby`, `NetProv`,
`NetUsers` and `NetAudio`. Two files it does not mention matter:

- **`NetJoin.cpp` is in scope and is the second largest piece** — 401 lines and
  42 DirectPlay call sites covering host, join, session enumeration, close and
  the four game flags. It is where session lifecycle actually lives.
- **`NetCrypt.cpp` is not in scope at all.** 265 lines of packet encryption
  with zero DirectPlay references. It operates on a `NETMSG` before it is sent
  and after it is received, so it survives the transport swap untouched.

## The good news: the data path is three functions

`NETsend`, `NETbcast` and `NETrecv` are the whole data path, and between them
they make exactly two DirectPlay calls:

- `IDirectPlayX_Send(glpDP, from, to, DPSEND_GUARANTEED | 0, msg, size)`
- `IDirectPlayX_Receive(glpDP, &idFrom, &idTo, DPRECEIVE_ALL, pMsg, &bufsize)`

with `DPID_ALLPLAYERS` for broadcast. That is already the interface the plan
proposes: send to one player or to all, reliable or not. The `NETMSG` framing
(`size`, `paddedBytes`, `type`, `body`) is the game's own and the encryption
layer sits above it, so the wire payload does not change.

So the data path is genuinely a small, well-contained rewrite. What follows is
everything that is not the data path.

## The work that is not the data path

**Session lifecycle.** `NetJoin.cpp` uses DirectPlay to enumerate sessions on
the LAN, open one as host or client, close it, and store four `DWORD` game
flags in the session description where joiners can read them *before* joining
(`NETgetGameFlagsUnjoined`). That last one is not incidental: the multiplayer
browser shows map, players and version from it. A replacement has to carry the
same four flags in whatever a discovery reply looks like.

**Player identity.** `DPID` is DirectPlay's player handle and it has leaked:
**113 references, 36 in NeuronCore and 77 across 15 files in `Outpost/`.** It
is in the public `PLAYER` struct, in `NETsend`'s signature, and in game code
that keys players by it. Replacing it with a plain host-assigned `UDWORD` is
mechanical but touches every one of those sites.

**Player data replication.** `NETgetLocalPlayerData` / `NETsetGlobalPlayerData`
and friends wrap DirectPlay's per-player replicated blobs. Only one consumer
exists — `MultiStat.cpp` replicating a `PLAYERSTATS` — but DirectPlay was doing
real work there: propagating a blob to everyone and keeping it available for
players who join later. That has to be built by hand or the feature dropped.

**System messages.** `NETrecv` recognises `DPID_SYSMSG` and hands the message
to `DirectPlaySystemMessageHandler` in `MultiPlay.cpp`, which switches on
`DPSYS_CREATEPLAYERORGROUP`, `DPSYS_DESTROYPLAYERORGROUP` and `DPSYS_HOST`.
Join, leave and host migration are all DirectPlay-generated events today. The
new transport has to generate equivalents, which means it needs its own notion
of a player joining, timing out, and the host going away.

**Address setup.** `NetProv.cpp` builds DirectPlay compound addresses for IPX,
TCP/IP, serial and modem. Everything but TCP/IP goes, and TCP/IP collapses
from a compound address to a host string.

**Lobby launch.** `NetLobby.cpp` writes registry entries so a DirectPlay lobby
can launch the game, and reads connection settings back when it does.
`bLobbyLaunched` is tested in nine places in `MultiInt.cpp` and `Config.cpp`.

## Verification is the problem

Every phase so far could be checked. Phase 1 was behaviour-preserving. Phase 3
was a link change I could confirm by reading the import libraries. Phase 4
compiles, links, and can be judged by ear once someone runs it.

**A network transport cannot be verified here at all.** The cross-check
compiles; CI compiles and links; neither runs the game, and nothing here can
run two of them and pass packets between them. A hand-written reliable-UDP
protocol — sequencing, acknowledgement, retransmission, ordering — that has
never exchanged a packet is not a migration step, it is an untested protocol
implementation carrying lockstep RTS commands, where a single reordered or
dropped command desynchronises the game silently.

That is not an argument against doing it. It is an argument for two things:

1. **A loopback harness has to be part of the phase, not an afterthought.** The
   transport should be written so that two instances can be driven in one
   process over the loopback interface, and a test can assert that a stream of
   reliable messages arrives complete and in order across simulated loss and
   reordering. That is buildable and runnable in CI on `windows-latest`, and it
   is the only thing standing between this change and a silent desync.
2. **The order should put the transport last, not first.** Everything that
   shrinks the surface — deleting voice chat, Mplayer, the lobby, the IPX and
   modem paths — is unambiguous, independently verifiable, and makes the
   transport smaller. Doing that first means the risky part is written against
   a codebase that is already 2,500 lines lighter.

## Proposed order

Each step builds and cross-checks on its own.

1. **Delete voice chat.** `NetAudio.cpp`, its nine entry points, the three
   `bCapture*` flags and the `AUDIOMSG` path. Self-contained, ~14 call sites,
   and it removes the `NETinitPlaybackBuffer(nullptr)` stop-gap Phase 4 put in.
   *(Done — see below.)*
2. **Delete Mplayer.** `MPDPXtra.cpp`/`.h` and `MPlayer.cpp`, `Mplayer.lib`,
   the registry probing and the lobby hooks. MigrationPlan puts this in Phase 6
   but calls it "naturally sequenced with Phase 5"; it is 1,484 lines of
   DirectPlay-shaped code for a service that shut down in 2001, and removing it
   before the transport rewrite means not porting any of it.
3. **Delete the dead address paths.** IPX, serial and modem in `NetProv.cpp`.
4. **Delete lobby launch.** `NetLobby.cpp` and the `bLobbyLaunched` branches.
5. **Define the transport interface.** A header with no DirectPlay in it:
   session create/enumerate/join/leave, player add/remove/timeout, reliable and
   unreliable send, receive. `NetPlay.h` keeps its public names so game code
   does not move yet.
6. **Retire `DPID`.** Introduce the id type the interface uses and sweep the
   113 sites. Mechanical, and verifiable by compiling.
7. **Write the transport, with the loopback harness alongside it.**
8. **Swap the implementation over and delete `dplayx.lib`, `dplay.h`,
   `dplobby.h`.**

Steps 1 to 4 remove roughly 2,500 lines and cannot break anything that is not
immediately visible at compile time. Steps 5 and 6 are shape changes with no
behaviour in them. Step 7 is the actual project.

## Decisions to confirm

These change the work materially, so they are listed rather than assumed.

1. **The loopback harness** — is a runnable test that CI executes acceptable as
   the bar for this phase? If not, the transport ships unverified and that
   should be a conscious choice rather than a discovered one.
2. **Player data replication** — rebuild `NET*PlayerData` over the new
   transport, or drop it and have `MultiStat.cpp` send its `PLAYERSTATS` as an
   ordinary broadcast message? The latter is far simpler and is what the one
   consumer actually needs.
3. **Host migration** — `DPSYS_HOST` says DirectPlay promoted us to host when
   the old one left. Reproducing that in a custom transport is real work.
   Alternative: the session ends when the host leaves, which is what most
   players will already expect.
4. **Mplayer removal timing** — with this phase as proposed above, or left in
   Phase 6 as MigrationPlan has it?
5. **Session discovery** — LAN broadcast on a fixed port plus direct connect by
   address. Any internet play (a master server, NAT traversal) is a separate
   project and is assumed out of scope.
6. **Encryption** — `NetCrypt.cpp` stays, but `bEncryptAllPackets` is `FALSE`
   at init and nothing sets it. Keep the layer, or remove it too?

## Progress

**Step 1 is done.** The rest is not started; steps 2 to 4 are unambiguous and
can proceed, and step 7 should not start before decision 1 is settled.
