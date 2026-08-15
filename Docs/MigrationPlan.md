# Migration Plan: DirectX 6/7 → Direct3D 9, C → C++

This document is the working plan for modernising Outpost.Warzone. It records
what the codebase looks like today, the order the work is being done in, and
why. Figures quoted here were measured, not estimated; the method is described
under [Verification](#verification).

## Target

The target is **Direct3D 9.0c**. There is no DirectX 9.1 — 9.0c is the final
DirectX 9 release, and it is what the SDK headers already vendored in `DX9/`
provide. Everything below assumes that target.

## Current state

| | |
|---|---|
| Source files | 206 `.cpp`, 378 `.h` |
| Translation units | 206 (85 NeuronCore, 121 Outpost) |
| Toolset | MSVC v145, Win32 (x86) only |
| Projects | `NeuronCore` (engine static lib), `Outpost` (game exe) |

The legacy DirectX surface is **well contained**: roughly 271 COM call sites,
almost all of them in ~15 NeuronCore files. Game code in `Outpost/` barely
touches DirectX directly. That containment is what makes this tractable.

Subsystems in use today:

- **Graphics** — DirectDraw 4 surfaces (`Screen.cpp`, `Surface.cpp`) plus a
  Direct3D 6 immediate-mode device (`LPDIRECT3DDEVICE3`) in `D3DRender.cpp`,
  `D3DMode.cpp`, `DX6TexMan.cpp`, `TexD3D.cpp`. The iVis layer (`RendMode.cpp`,
  `PieState.h`) still carries dead 3dfx Glide, software-render and 8-bit
  palette backends.
- **Input** — DirectInput 7 (`DXInput.cpp`, `DIRECTINPUT_VERSION=0x0700`).
- **Audio** — QSound's QMixer (`QSTrack.cpp`, `Aud.cpp`) over DirectSound, plus
  MCI CD audio (`CDAudio.cpp`) and CD spanning (`CDSpan.cpp`).
- **Video** — `Sequence.cpp` streams `.rpl` movies onto DirectDraw surfaces via
  `WINSTR.LIB`.
- **Network** — DirectPlay 4 (`NetPlay.cpp`, `NetSupp.cpp`, `NetLobby.cpp`,
  `NetProv.cpp`, `NetUsers.cpp`, `NetAudio.cpp`), plus Mplayer.com matchmaking
  (`MPDPXtra.cpp`, `MPlayer.cpp`).

## Ordering: C++ first, then DirectX

The C++ conversion is done **before** the DirectX migration, for three reasons.

1. **The D3D9 graphics work is a rewrite, not a port.** DirectDraw does not
   exist in D3D9 — no `Blt`, no flipping chains, no palettes, no `GetDC` on
   surfaces. That new code should be written once, in C++, with natural
   `device->Method(...)` syntax, RAII for COM lifetimes, and the D3DX helpers
   that are painful to use from C.

2. **The COM code does not need converting during the C++ pass.** This is the
   decisive point. Defining `CINTERFACE` keeps the C-style vtable structs
   visible in C++, so the legacy DirectX files cross into C++ essentially
   untouched. Measured effect on error counts:

   | File | Without `CINTERFACE` | With `CINTERFACE` |
   |---|---|---|
   | `D3DRender.cpp` | 71 | 2 |
   | `DX6TexMan.cpp` | 48 | 4 |
   | `Surface.cpp` | 20 | 0 |

   So the ~271 `lpVtbl` call sites are **not** hand-converted to C++ COM
   syntax — they stay as they are behind `CINTERFACE` and are rewritten
   properly against D3D9 in Phase 2. This decouples the two migrations almost
   entirely and avoids churning code that is about to be deleted.

3. **The C++ compile pass is behaviour-preserving, so it is cheap to verify.**
   Doing it while the code is still DX6/7 means any breakage is unambiguous.

Full C++ *modernisation* (classes, RAII, `std::` types) is deliberately last —
doing it earlier would refactor the very modules Phase 2 throws away.

---

## Phase 0 — Baseline and decisions

Establish a known-good build and a reference run (menus, a skirmish, save and
reload) to compare against. Lock the decisions that simplify everything
downstream:

- Drop the Glide, software-render and 8-bit palette paths from iVis; D3D9 only.
- 32-bit colour as primary. The 16-bit assumptions in `Sequence.cpp` and the
  pixel-format code need auditing.
- Stay x86 for now — the remaining third-party binaries are 32-bit.

## Phase 1 — C → C++ compile pass

Rename `.c` → `.cpp` (via `git mv`, preserving history) and fix **only**
compile errors. No refactoring and no behaviour change in this phase; that is
what keeps the diff reviewable and verification trivial.

**Status: complete and verified.** All 207 units are `.cpp`, and both Win32
configurations compile and link under MSVC. Every commit up to the rename kept
the sources valid C, so CI stayed green through that half and each step was
independently verifiable; the rename and the commits after it were red until
the conversion finished. What it took, roughly in order of size:

- **`typedef signed char STRING` was the dominant issue.** Because
  `signed char*` and `char*` are distinct types in C++, this one typedef
  produced roughly 11,000 of the 12,781 total diagnostics. Changing it to
  plain `char` — identical in representation, since MSVC's default `char` is
  signed — took the tree from **198 failing units to 81** on its own.
- **The script id spaces were a design mismatch, not 300 separate casts.**
  `INTERP_TYPE`, `TRIGGER_TYPE` and `SECONDARY_STATE` are open ended:
  `VAL_USERTYPESTART` and `TR_CALLBACKSTART` are extension points the game
  continues numbering from, and `SECONDARY_STATE` is a bit flag set that gets
  masked. Closed enum types cannot express that in C++. Their constants moved
  into anonymous enums and the types became integer typedefs, which fixed all
  of them at once, kept the 122 switch case labels working and left `sizeof`
  unchanged.
- **Cast-as-lvalue** (`(UDWORD)(p->field) = id`, `&((UBYTE)value)`) is an MSVC
  C-only extension and an error in C++ even on MSVC. 34 sites: the save-game
  pointer fixup in `Game.cpp`, and the `NetAdd` call sites, which now use
  width-explicit `NetAddType`/`NetAdd2Type` macros so the bytes on the wire are
  unchanged.
- **29 functions were called with no visible declaration** — C inferred a
  signature, C++ will not. Thirteen callers were missing an include; the other
  sixteen had no declaration anywhere and now have one beside their peers.
- **The generated lexers and parsers** needed 18 K&R definitions converted to
  prototypes and 10 parser entry points given explicit `int` returns. This was
  far smaller than the regeneration project it first looked like, so the
  generated sources are patched in place; regenerating them would need MKS
  Lex/Yacc, which the `.l` and `.y` sources have long outlived.
- **Smaller classes:** 30 ambiguous `abs(UDWORD)` calls, 13 implicit-`int`
  declarations, and assorted handle and pointer type mismatches.
- **REFGUID last.** The DirectX headers make `REFGUID`/`REFIID` a
  `const GUID *` in C but a `const GUID &` in C++, so 28 call sites had to
  change form. This is the one change that could not precede the rename, and
  it landed with it.

Two latent defects surfaced along the way. `Display.cpp` tested modifier keys as
`keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL || keyDown(KEY_LSHIFT) || ...)` — a
misplaced parenthesis that collapsed the chain to a boolean, so it asked
whether key 1 was down rather than testing right control and the shift keys.
That is fixed, and is the only behavioural change in the phase. Four
declaration/definition mismatches were also genuine bugs.

Two things that looked wrong are not: `pie_SetRendMode(TRANS_DECAL)` passes the
wrong enum type but both constants are zero, and `displayCamTypeBut` printing
`&UserData` as a string is a deliberate little-endian trick, since `UserData`
holds a single character. Both were preserved and merely typed correctly.

`CINTERFACE` is defined for both projects, which keeps the roughly 270 legacy
`lpVtbl` COM call sites compiling untouched. They are rewritten against
Direct3D 9 in Phase 2 rather than converted twice.

### What only the real build could find

The cross-check cleared the tree three times before MSVC agreed, each time for
a reason it could not have seen. These are worth remembering, because Phase 2
faces the same asymmetry:

- **`__STDC__`.** The generated lexers gate their prototypes on it. GCC must
  define it; MSVC does not without `/Za`, so `YY_ARGS` collapsed to `()` and
  every call to `res_error` and its siblings failed. GCC always took the other
  branch.
- **Implicit `int`.** GCC accepts `extern foo(void);` in C++ silently, without
  even a warning. MSVC rejects it. No flag closes the gap, so the harness
  gained a textual scan for that shape.
- **Linking.** The largest class by far, and one the cross-check cannot reach
  at all, having no linker. C merged a global written without `extern` in a
  header, or the same file-scope definition repeated in two sources, into a
  single common symbol; C++ makes each a real definition. That covered
  `psActiveBullets`, `titleMode`, `hWndMain`, `mX`/`mY`, the drag-box `POINT`
  scratch, the four generated parsers' shared `yy*` state, and a stray
  `LTOKEN_TYPE` variable left by a missing `typedef`.

Name mangling also exposed two mismatches that C had been linking happily for
years: `buildTime` defined `UBYTE[9]` and declared `char[8]`, and
`intAddTemplateButtons` called with eight arguments against seven parameters.

## Phase 2 — Direct3D 9 graphics

The largest phase.

- **Device.** Replace DirectDraw enumeration, cooperative levels and flipping
  in `Screen.cpp`/`D3DMode.cpp` with `Direct3DCreate9` → `CreateDevice` and
  `D3DPRESENT_PARAMETERS`. Budget real time for **device-lost/reset
  handling** — a failure mode DX6 code has no concept of, which forces
  tracking of which resources must be recreated on reset.
- **Textures.** Rewrite `DX6TexMan.cpp`, `TexD3D.cpp` and `Surface.cpp` around
  `IDirect3DTexture9` in the managed pool, which also deletes the old texture
  paging logic.
- **3D draw path.** `D3DRender.cpp` maps fairly directly:
  `D3DRENDERSTATE_*` → `D3DRS_*`, texture stage states carry over,
  `D3DTLVERTEX`-style formats become FVFs, and `DrawPrimitive` becomes
  `DrawPrimitiveUP` first (optimise to dynamic vertex buffers later).
  Fixed-function only; no shaders are needed for parity.
- **2D path.** UI blits, fonts and cursor (`TextDraw.cpp`, `Font.cpp`,
  `BitImage.cpp`, `Cursor.cpp`, `Disp2D.cpp`) move from surface `Blt` to textured
  quads or `ID3DXSprite`; fonts via `ID3DXFont` or a baked glyph atlas.
- **Video.** `Sequence.cpp` decodes into a dynamic texture drawn as a quad; its
  software and 3dfx paths go away. See also Phase 6 — the decoder itself is
  being replaced.

Milestones, in order: clear + present → menus/UI → 3D world → a visual parity
checklist (transparency, additive effects, fog, terrain).

## Phase 3 — DirectInput 7 → 8

Small and mechanical: `DIRECTINPUT_VERSION=0x0800`, `DirectInput8Create`,
updated interface names in `DXInput.cpp`, link `dinput8.lib`. An afternoon, not a
project.

## Phase 4 — Audio: XAudio2, dropping QMixer and CD audio

**Done.** The earlier plan kept the option of retaining QMixer; that option was
dropped. Audio is on **XAudio2**, and QMixer and the CD audio features have
been removed outright.

`QSTrack.cpp` is replaced by `XA2Track.cpp` behind an unchanged `TrackLib.h`;
`QMIXER.H`, `QMixer.lib`, `QMixer.dll` and the vestigial `EAX.H` are gone, as
are `CDAudio.cpp` and `Mixer.cpp`. In-game music is served from files on disk
by `Music.cpp`, and both volume sliders now move the XAudio2 graph rather than
the Windows system mixer. `CDSpan.cpp` stays: removing it rests on game data
being installed to disk, which is a decision of its own.

What it took, what was decided and what is still unverified are in
[Phase4Plan.md](Phase4Plan.md).

This is well contained because `TrackLib.h` is already a clean ~80-line
interface: `QSTrack.cpp` (849 lines) is simply its QMixer implementation. The
port replaces that implementation behind the existing interface.

- **Replace** `QSTrack.cpp` with an XAudio2 backend implementing `TrackLib.h`:
  a mastering voice plus pooled source voices, with per-sample volume, pan and
  frequency mapped onto voice parameters. 3D positional audio, currently
  QMixer's `bScale3D` path, becomes X3DAudio or a simple attenuation model —
  whichever matches the existing behaviour more closely.
- **Remove** `QMixer.lib`, `QMixer.dll`, `QMIXER.H` and the QSound references
  in `Aud.cpp`. Drop `EAX.H`, which is vestigial.
- **Remove CD audio.** `CDAudio.cpp` (230 lines) and its MCI usage go; in-game
  music is served from files on disk instead. `CDSpan.cpp` (631 lines) is
  related but distinct — it locates game data across multiple CDs. It should
  be removed too if game data is installed to disk, but that is a **decision
  to confirm** rather than an assumption; `cdspan_*` calls are referenced from
  game code and need unpicking.
- Review `Audio.cpp` (1289 lines), `Track.cpp` (703) and `Mixer.cpp` (283) for
  QMixer-shaped assumptions leaking above the `TrackLib.h` line.

DirectSound itself is unaffected by the D3D9 work, so this phase is severable
and can run in parallel with Phase 2.

## Phase 5 — Networking: custom UDP transport on WinSock2

**Revised.** DirectPlay 4 is removed entirely and replaced with a **custom
UDP-based transport layer implemented on WinSock2**. Porting to DirectPlay 8
is explicitly rejected — it would be nearly a full rewrite into another
deprecated API.

- Define a small transport interface covering what `NetPlay.cpp` / `NetSupp.cpp`
  actually need: session create/enumerate/join, player add/remove, and
  reliable plus unreliable sends.
- Implement it over WinSock2 UDP. Since the game's message layer already
  frames its own `NETMSG` payloads, the transport needs sequencing,
  acknowledgement and retransmission only for the traffic that requires
  reliable, ordered delivery; lockstep-critical game commands must keep the
  ordering guarantees DirectPlay provided today. Session discovery becomes LAN
  broadcast plus direct connect by address.
- **Remove** `dplayx.lib`, `dplay.h`/`dplobby.h`, and rewrite `NetProv.cpp`
  (service provider enumeration) and `NetLobby.cpp` (lobby launch), which are
  DirectPlay-shaped and largely disappear.
- `NetAudio.cpp` (voice chat) is dropped rather than ported.
- The `NetAdd`/`NetGet` message macros and the `NETMSG` layout are unchanged,
  which keeps the blast radius inside the transport.

## Phase 6 — Removing Mplayer.lib and WINSTR.LIB

**New item.** Two third-party static libraries remain and both must go. They
are unrelated to each other despite being listed together, and the second is
considerably more involved than its name suggests.

### Mplayer.lib — dead matchmaking service

`Mplayer.lib` is the Mplayer DirectPlay Extras library from Mpath Interactive
(© 1996-97), for the Mplayer.com online gaming service. **That service shut
down in 2001**, so this is pure dead weight.

- Delete `MPDPXtra.cpp` (751 lines), `MPDPXtra.h` (662 lines) and `MPlayer.cpp`
  (86 lines), and drop them from `Outpost.vcxproj`.
- Remove `Mplayer.lib` from `AdditionalDependencies`, and the Mplayer registry
  probing and lobby hooks in `NetLobby.cpp`.
- Low risk: the coupling is limited to `MPlayer.cpp`, which handles stat
  submission to the service.
- Naturally sequenced with Phase 5, since both concern DirectPlay-era
  networking.

### WINSTR.LIB — the FMV video codec

**This is not a string library.** Despite the name, `WINSTR.LIB` (and
`GameData/winstr.dll`) is Eidos' video streaming library: 64 exports in the
`Movie_*`, `Alpha_*` and `Streamer_*` families, declared in `STREAMER.H`. It is
consumed by exactly one file, `NeuronCore/Sequence.cpp`, and it decodes the
game's `.rpl` movies — the briefings and research sequences under
`GameData/sequences/`. `GameData/Dec130.dll` appears to be the associated
decoder.

Removing it therefore means **replacing FMV playback**, not deleting a utility.
The `.rpl` assets cannot be decoded without a replacement, so the format they
migrate to is a decision in its own right.

#### The assets

19 sequences, **15.7 MB** in total, the largest 1.4 MB. They are
320x240-era content, and each carries **audio as well as video** —
`Sequence.cpp` reads `Movie_GetSoundChannels`, `Movie_GetSoundPrecision` and
`Movie_GetSoundRate`, and feeds a DirectSound buffer alongside the frames. Any
replacement has to carry the audio and keep it in sync, not just the pictures.

#### Considered and rejected: frames as DDS textures

The appeal is obvious — store the frames as textures and playback needs no
decoder at all, just a sample per frame into the quad Phase 2 introduces. Two
things rule it out as the general answer.

**There is no `Texture2DArray` in Direct3D 9.** Texture arrays arrived with
Direct3D 10. D3D9 offers only `IDirect3DTexture9`,
`IDirect3DVolumeTexture9` and `IDirect3DCubeTexture9`. The nearest equivalents
each have a catch:

| Approach | Catch |
|---|---|
| Volume texture, frames on Z | Trilinear filtering blends adjacent *frames*; `MaxVolumeExtent` caps are low |
| Texture atlas, frames tiled | Bounded by `MaxTextureWidth`/`MaxTextureHeight` |
| One DDS per frame, streamed | No caps problem, but thousands of files per sequence |

**The size cost is severe.** Block compression is per-frame and has no temporal
compression, which is where video codecs get nearly all their ratio. At DXT1
(0.5 byte/pixel), a 320x240 frame is ~38 KB, so one 60-second sequence at 15 fps
is ~34 MB — twice the entire current set, for one sequence. Expect **50-100x
growth** overall, plus DXT blocking artefacts on exactly the kind of gradient
content these sequences contain. And DDS carries no audio track.

Where this approach *does* fit is short UI animations and effect flipbooks:
small frame counts, no audio, and decode-free sampling is genuinely simpler
there. It is worth keeping in mind for Phase 2's effects work, just not for FMV.

#### The plan: re-encode to a modern container

Convert the `.rpl` assets offline to a modern container, and play them back
through Media Foundation (or a small bundled decoder) into the D3D9 dynamic
texture Phase 2 introduces. This keeps the asset footprint in the same order as
today, keeps the audio track with its sync, and still removes `WINSTR.LIB`
entirely. Since the source material is 320x240, a modern codec will not look
worse than what ships now.

Two things to settle when the work starts: whether a decoder ships with the
game or Media Foundation's built-in support is relied on, and whether the
original `.rpl` files are kept in the repository as the conversion source.

Dropping FMV altogether — static screens in place of the sequences — remains
the cheap fallback if the conversion proves not to be worth it, at the cost of
game content.

Either way the work is tightly coupled to Phase 2's rewrite of `Sequence.cpp`,
so the two should be scheduled together. Once done, remove `WINSTR.LIB`,
`winstr.dll`, `STREAMER.H` and `Dec130.dll`.

After Phases 4-6 the only remaining non-system dependencies are the DirectX
libraries themselves — which also removes the last constraint pinning the build
to 32-bit, making an x64 target viable.

## Phase 7 — Incremental C++ modernisation

Once the churn is over: COM smart pointers replacing manual `Release` chains,
namespaces per module, converting struct-plus-function-table modules into
classes where it genuinely pays, and `std::vector`/`std::string` in leaf code.

Two constraints: any struct serialised into save files must stay
byte-identical, and this should proceed module by module indefinitely rather
than as a big bang.

### The legacy debug system is gone

**Done, out of order.** `LegacyDebug.cpp`, `W95Trace.cpp` and `Mono.cpp` have
been removed along with their headers, and the roughly 3,000 call sites now use
the calls in `Debug.h`.

| was | now |
|---|---|
| `ASSERT((cond, "msg", a))` | `ASSERT_TEXT(cond, "msg", a)` |
| `DBERROR(("msg", a))` | `Neuron::Fatal("msg", a)` |
| `DBPRINTF(("msg", a))`, `DBMB` | `Neuron::DebugTrace("msg", a)` |
| `DBPn(("msg", a))` | `Neuron::DebugTrace`, or deleted where the group was off |

`W95Trace.cpp` was a Win95 stack-trace helper and `Mono.cpp` drove a second
monochrome monitor over an MDA card; both had no callers left. The file-output
and mono macros - `DBOUTPUTFILE`, `DBMONOPRINTF` and their siblings - were
already at zero uses.

`DBERROR` reported an error and continued, and `Debug.h` has no non-fatal
equivalent, so those sites now terminate. Most of them were followed by a
`return FALSE` that is consequently unreachable; the builds do not use `/WX`,
so the unreachable-code warnings do not break them.

`dbg_printf` took printf conversions and `std::format` takes replacement
fields, so about a thousand format strings were rewritten as well - `%s` and
`%d` to `{}`, `%04x` to `{:04x}`, `%-6d` to `{:<6}`. This is the part that
needed the compiler rather than review: `std::format` refuses to format an
enum, a `UBYTE *` or a typed pointer, all of which printf accepted silently,
and each one is a compile error until it is cast.

Four things needed more than a substitution:

- **`DEBUG` moved into `Debug.h`.** `LegacyDebug.h` defined it, and it gates
  80-odd blocks, several in headers where it changes a struct's layout - so
  every translation unit has to agree on it.
- **`FPath.cpp`, `Move.cpp` and `GatewayRoute.cpp` redefine `DBPn`** to
  something gated on a runtime flag. Their calls are live where the same macro
  is dead elsewhere, and they now use their own `FPATH_TRACE`, `MOVE_TRACE` and
  `GWR_TRACE` macros, which keep the flag.
- **Seven `DBERROR((FALSE, "msg"))` sites** passed `FALSE` where
  `dbg_ErrorBox` expected the format string, having been written as though they
  were assertions. Reaching one would have handed `vsprintf` a null format.
  They pass the message now.
- **`Debug.h` itself.** `vformat` was unqualified, `Fatal` formatted a message
  and then threw it away, and `DEBUG_WARNING` called an unqualified
  `DebugTrace`. `Fatal` is the release-visible error path now, so it outputs
  the message before breaking.

### The custom allocators are gone

**Done, out of order.** `Mem.cpp`, `Heap.cpp` and `Block.cpp` were three
allocators layered over `malloc`, and all three have been removed along with
`Mem.h`, `MemInt.h`, `Heap.h`, `Block.h` and the `PTRVALID` pointer checks.
They came out early because they sat underneath everything else and made every
other module harder to reason about.

- **`Mem.cpp`** wrapped `malloc` with a treap of live blocks, 32-byte guard
  bands and fill patterns — a debug heap, and a slow one. It only did any of
  that under `DEBUG_MALLOC`; release builds paid for the indirection and got
  nothing. `MALLOC`/`FREE` became typed `new`/`delete[]`.
- **`Heap.cpp`** was a free-list pool per object type. Each of the thirty-odd
  heaps held exactly one type, so `HEAP_ALLOC` maps onto `new` of that type
  with nothing lost but the pooling.
- **`Block.cpp`** was a bump allocator that `MALLOC` diverted into while a
  block was "current"; `blkFree` did nothing and memory came back wholesale on
  `BLOCK_RESET`. Level data is now released through the resource system's own
  per-type release functions, which `resReleaseBlockData` already called on
  every level change.

`new (std::nothrow)` is used throughout rather than plain `new`, so allocation
failure still returns null the way `malloc` did and the existing out-of-memory
checks keep working instead of becoming dead code behind an uncaught
`bad_alloc`.

Allocations take the array form (`new T[n]`, and `new T[1]` for a single
object) wherever the allocation and its release are not visibly paired, which
is most of them: buffers are handed out through out-parameters, aliased into
locals under another name, and freed through a base type that shares only a
layout prefix. With one deallocation form the two ends cannot disagree. On a
trivially-destructible type `new T[1]` allocates exactly `sizeof(T)` with no
cookie, so the cost is how it reads, not what it does.

Three things need watching, and they are all consequences of the C-style
object model rather than of the change itself:

- **Deleting through a C-style base is wrong.** `BASE_OBJECT`, `SIMPLE_OBJECT`
  and `BASEANIM` are built from the same `*_ELEMENTS` macros as the types that
  "derive" from them, but they are unrelated types that merely start the same
  way. `delete` through one passes the wrong size to `operator delete`, so
  those sites cast to the real type first.
- **Double frees used to be survivable and no longer are.** Freeing block-heap
  memory twice hit `blkFree`, which did nothing. Both frees are now real.
- **What only `BLOCK_RESET` reclaimed now leaks.** Anything allocated into a
  block heap and never explicitly freed relied on the wholesale reset. The
  resource system covers the level data; anything else will show up as growth
  across level changes.

`iV_HeapAlloc`/`iV_HeapFree` in `IvisPatch.h` stay on `malloc`/`free`: they
hand out untyped bytes, so there is no type for `new` to allocate.

## Verification

There is no MSVC or Windows SDK in the Linux development container, so a real
build baseline is not available there. Instead, every translation unit is
syntax-checked with **mingw-w64** against a shadow copy of the tree, using the
include paths and preprocessor definitions taken from the `.vcxproj` files.
`tools/crosscheck.py` is the harness. The shadow neutralises the Windows-only
things GCC cannot process: includes whose case does not match the real
filename, the Concurrency Runtime headers `NeuronCore.h` includes but never
uses, and — under `tools/stubs/` — declarations for the headers mingw-w64
does not ship at all, currently `x3daudio.h`. Those last are a transcription
of somebody else's API and check our use of it rather than themselves; each
says so at the top. The inline-assembly handling has gone with the last `__asm` in the
tree, and so has the `CINTERFACE` define — nothing in the tree defines it any
more, and leaving it in the harness failed seven units against a green build.

This is a **proxy, not MSVC**. GCC is stricter in some places, MSVC under
`/permissive` is more lenient in others, and the harness cannot link (QMixer,
Mplayer and WINSTR are 32-bit MSVC binaries). It reliably catches the portable
C++ issues, which is what Phase 1 is about, but a real `msbuild` remains the
final word.

Current state: **200 of 200 units clean under the cross-check**, and both
Win32 CI builds green. The unit count moves as files are added and removed;
Phase 4 took out five and added two. Treat the cross-check as a fast first pass, not a
verdict: it is a different compiler, it cannot link, and the section above
lists what that costs. The CI builds remain the authority.
