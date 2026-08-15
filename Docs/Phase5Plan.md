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
| `NeuronCore/NetCrypt.cpp` | 265 | **0** | mostly **stays** — only its packet half is QUIC's to replace |
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

*This is what moved the transport to QUIC.* Sequencing, acknowledgement,
retransmission and ordering become somebody else's tested code, and what is
left to write here is session management, which fails visibly rather than
silently. The argument below still holds for that remainder:

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

## Decisions, as settled

**The transport is QUIC, via MsQuic.** Not a hand-written reliable-UDP
protocol. MsQuic supplies what DirectPlay supplied and what would otherwise
have had to be written and debugged blind:

| DirectPlay | QUIC / MsQuic |
|---|---|
| `DPSEND_GUARANTEED` | a reliable ordered stream |
| unguaranteed send | a QUIC DATAGRAM (RFC 9221) |
| session open/close, player timeout | connection lifecycle and idle timeout |
| nothing | TLS 1.3 on every byte |

What stays hand-written is the part QUIC does not do: **LAN discovery over raw
UDP broadcast**, and the session and player layer above the connections.

1. **TLS backend — Schannel, and the floor becomes Windows 11.** MsQuic's
   default. TLS comes from the OS, so nothing extra ships. Its docs are
   explicit that Schannel TLS 1.3 needs Windows 11 or Server 2022, which
   raises the Windows 10 floor Phase 4 set for XAudio 2.9. Defensible: Windows
   10 passed end of support in October 2025. The alternative was building the
   quictls OpenSSL fork for x86, which would have been the largest
   build-system change in the migration.
2. **MsQuic arrives as a NuGet package** — `Microsoft.Native.Quic.MsQuic.Schannel`.
3. **Topology — the host relays.** Host is the QUIC server, every client holds
   one connection to it, and a client sending to another client goes through
   the host. One connection per player and one certificate. Most of the game's
   traffic is already broadcast, so the extra hop applies to a minority of it.
4. **Certificates — self-signed, generated when the host opens a session**,
   with clients passing `QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION`. Be
   plain about what this is worth: traffic is encrypted against passive
   eavesdropping, but an active attacker on the same LAN can impersonate a
   host. It is still strictly better than DirectPlay, which sent everything in
   clear.
5. **Verification — a loopback harness, run in CI.** Two transport instances in
   one process over loopback, asserting reliable messages arrive complete and
   in order under simulated loss, duplication and reordering. This is the bar
   for the phase.
6. **Host migration — the session ends when the host leaves.** The `DPSYS_HOST`
   branch is deleted rather than reimplemented.
7. **Player data — replication is dropped.** `NETget/setLocal/GlobalPlayerData`
   go, and `MultiStat.cpp` broadcasts its `PLAYERSTATS` as an ordinary message.
8. **Encryption — `NetCrypt.cpp` stays, and only its packet half goes, at the
   swap.** This decision was made twice and got it wrong both times; the file
   as it stands is the third answer and the correct one.

   The claim that `bEncryptAllPackets` is never set was wrong — it was grepped
   in `NetPlay.cpp` alone. `MultiOpt.cpp` sets it `TRUE` once the game starts
   and `FALSE` on the way back to the menu, and `MultiPlay.cpp` suspends it
   around chat. Packet encryption is **live in every multiplayer game**, so
   removing it before QUIC exists would put game traffic in clear in the
   interim.

   And `NetCrypt.cpp` is not only packet encryption. It provides four things
   and QUIC supersedes exactly one:

   | | |
   |---|---|
   | `NETmanglePacket` / `NETunmanglePacket` | the wire — QUIC replaces this, at the swap |
   | `NETmangleData` / `NETunmangleData` | obfuscates the `.sta` player-stats file on disk |
   | `NEThashFile` / `NEThashVal` | hashes the executable; players compare it at join to catch a mismatched binary |
   | `NEThashBuffer` | backs `Data.cpp`'s cheat hashing over loaded data files |

   Three of those four have nothing to do with networking. Deleting the file
   would have broken stats loading, the version check and the cheat hash.

### What was verified rather than assumed

The choice above rests on facts that would be expensive to discover late, so
they were checked first:

- **The NuGet package ships Win32 x86.** `build/native/lib/x86/msquic.lib` and
  `build/native/bin/x86/msquic.dll`, alongside x64 and arm64, at version 2.6.0.
  This mattered more than anything else: the build is x86-only until Phase 6
  removes `WINSTR.LIB`, so an x64-only package would have forced Phase 5 to
  wait for Phase 6.
- **Its `.targets` file already handles `Win32`.** It adds the include
  directory, links `lib/x86/msquic.lib` when the platform is Win32, and copies
  `bin/x86/msquic.dll` next to the executable after build. So the vcxproj
  change is an import and a `packages.config`, plus a restore step in CI.
- **Unreliable sends exist.** `QUIC_DATAGRAM_SEND_STATE` and
  `QUIC_PARAM_CONN_DATAGRAM_RECEIVE_ENABLED` are in `msquic.h`, so the
  unguaranteed half of `NETsend` has somewhere to go.
- **The certificate escape hatch exists.** `QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION`
  is a documented flag, not a workaround.

The `DPID` sweep shipped red and CI caught it, which is worth recording
because the harness is why it was not caught sooner. `NETPLAYERID` is a
`UDWORD` and `DPID` a `DWORD` — both 32 bits, different types — so a
`NETPLAYERID*` where DirectPlay wants an `LPDPID`, or a callback taking one
instead of the other, is an error MSVC refuses and GCC merely warns about under
`-fpermissive`. `-w` then hid the warning. Seven sites across three files
passed the cross-check and failed the build.

`-fpermissive` cannot simply go: this codebase names anonymous types with
`using X = enum {...}`, which MSVC accepts and GCC rejects, in 81 units.
`crosscheck.py` now runs a second pass with warnings visible and fails on the
diagnostics `-fpermissive` downgrades — currently `invalid conversion` and
`cannot convert` — scoped to our own files, so the vendored msquic headers do
not trip it on a mingw gap. The check was verified by reintroducing one of the
seven and confirming the harness goes red.

One gap between mingw-w64 and the real headers turned out to matter to the
real build too, and is recorded here because it is the kind of thing that would
otherwise be rediscovered painfully. `msquic_winuser.h` writes `#if DEBUG` to
pick the debug layout of `QUIC_SQE`. `Debug.h` — reached from `pch.h` long
before any of this — defines `DEBUG` with no value, which makes that `#if` with
no expression: a preprocessor error, on MSVC as much as on GCC. `NetQuic.cpp`
pushes and undefines the macro around the include, which is also the correct
value, since the `msquic.dll` the package ships is a release build.

Two things remain unverified until CI runs. Whether the `windows-latest` runner
is new enough for Schannel TLS 1.3 — it should be Server 2022 or later, and if
it is not, the loopback harness fails loudly at connection open, which is the
right way to find out. And mingw-w64 has no `msquic.h`, so the cross-check will
need a checked-in stub under `tools/stubs/` exactly as `x3daudio.h` did, with
the same caveat: it checks our use of the API, not the API.

## Revised order

Unchanged in shape — deletions first, so the risky part is written against a
smaller codebase — but step 7 is now a port onto MsQuic rather than a protocol
project.

1. **Delete voice chat.** *(Done.)*
2. **Delete Mplayer** — `MPDPXtra.cpp`/`.h`, `MPlayer.cpp`, `Mplayer.lib`.
3. **Delete the dead address paths** — IPX, serial, modem in `NetProv.cpp`.
4. **Delete lobby launch** — `NetLobby.cpp` and the `bLobbyLaunched` branches.
5. **Define the transport interface**, with no DirectPlay in it.
6. **Retire `DPID`** across its 113 sites.
7. **Add MsQuic**, write the transport and the loopback harness together.
8. **Swap over**: delete `dplayx.lib`, `dplay.h`, `dplobby.h`, the connection
   screen's modem and serial paths, and `NETmanglePacket` — everything that has
   a QUIC replacement only once the replacement is actually there.

## Progress

**Steps 1 to 6 are done.** `NetTransport.h` is the seam, `NETPLAYERID` has
replaced `DPID`, and DirectPlay's reach is down from fifteen files to ten — of
which only two are game code rather than the `Net*` modules: `MultiInt.cpp`'s
connection-type screen and `MultiPlay.cpp`'s system-message switch, both of
which the swap rewrites.

Earlier: **steps 1 to 4** — voice chat, Mplayer, the DirectPlay lobby and the
dead IPX address setup are gone, about 2,600 lines. Two things the plan got
wrong turned up while doing them, and both are recorded above rather than
quietly fixed: the modem and serial address paths are not independently
removable because the connection screen still lists them, and `NetCrypt.cpp`
is neither dead nor purely networking. Step 7 is the project.
