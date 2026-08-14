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
| Source files | 207 `.c`, 1 `.cpp`, 382 `.h` (~486k lines) |
| Translation units | 208 (88 NeuronCore, 120 Outpost) |
| Toolset | MSVC v145, Win32 (x86) only |
| Projects | `NeuronCore` (engine static lib), `Outpost` (game exe) |

The legacy DirectX surface is **well contained**: roughly 271 COM call sites,
almost all of them in ~15 NeuronCore files. Game code in `Outpost/` barely
touches DirectX directly. That containment is what makes this tractable.

Subsystems in use today:

- **Graphics** — DirectDraw 4 surfaces (`Screen.c`, `Surface.c`) plus a
  Direct3D 6 immediate-mode device (`LPDIRECT3DDEVICE3`) in `D3DRender.c`,
  `D3DMode.c`, `DX6TexMan.c`, `TexD3D.c`. The iVis layer (`RendMode.c`,
  `PieState.h`) still carries dead 3dfx Glide, software-render and 8-bit
  palette backends.
- **Input** — DirectInput 7 (`DXInput.c`, `DIRECTINPUT_VERSION=0x0700`).
- **Audio** — QSound's QMixer (`QSTrack.c`, `Aud.c`) over DirectSound, plus
  MCI CD audio (`CDAudio.c`) and CD spanning (`CDSpan.c`).
- **Video** — `Sequence.c` streams `.rpl` movies onto DirectDraw surfaces via
  `WINSTR.LIB`.
- **Network** — DirectPlay 4 (`NetPlay.c`, `NetSupp.c`, `NetLobby.c`,
  `NetProv.c`, `NetUsers.c`, `NetAudio.c`), plus Mplayer.com matchmaking
  (`MPDPXtra.c`, `MPlayer.c`).

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
   | `D3DRender.c` | 71 | 2 |
   | `DX6TexMan.c` | 48 | 4 |
   | `Surface.c` | 20 | 0 |

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
- 32-bit colour as primary. The 16-bit assumptions in `Sequence.c` and the
  pixel-format code need auditing.
- Stay x86 for now — the remaining third-party binaries are 32-bit.

## Phase 1 — C → C++ compile pass

Rename `.c` → `.cpp` (via `git mv`, preserving history) and fix **only**
compile errors. No refactoring and no behaviour change in this phase; that is
what keeps the diff reviewable and verification trivial.

**Status: in progress.** Groundwork is committed and the C build is verified
clean across all 208 units. Findings so far:

- **`typedef signed char STRING` was the dominant issue.** Because
  `signed char*` and `char*` are distinct types in C++, this one typedef
  produced roughly 11,000 of the 12,781 total C++ diagnostics. Changing it to
  plain `char` — identical in representation, since MSVC's default `char` is
  signed — took the tree from **198 failing units / 12,781 errors** to
  **81 / 851**.
- Cast-as-lvalue (`(UDWORD)(p->field) = id`, `&((UBYTE)value)`) is an MSVC
  C-only extension and is an error in C++ even on MSVC. 34 sites fixed: the
  save-game pointer fixup in `Game.c`, and the `NetAdd` call sites, which now
  use width-explicit `NetAddType`/`NetAdd2Type` macros so the bytes written on
  the wire are unchanged.
- Four implicit-`int` declarations and four declaration/definition mismatches
  were genuine latent bugs, now fixed.

Remaining Phase 1 work:

- ~330 script-system enum conversions (`INTERP_TYPE`, `TRIGGER_TYPE`,
  `SECONDARY_STATE`) — bulky but mechanical.
- The four checked-in generated lexers (`parser_l.c`, `resource_l.c`,
  `Script_l.c`, `StrRes_l.c`) use K&R-style definitions from a 1990s MKS
  Lex/Yacc. **Decision needed:** regenerate with modern flex/bison, or patch
  the generated output.
- Small tails: ambiguous `abs(UDWORD)` (30), `min`/`max` (23).
- The rename itself, plus updating both `.vcxproj` files.

Note that the verification harness is a cross-compiler proxy, so the first real
`msbuild` after the rename will surface a residue of MSVC-specific issues.

## Phase 2 — Direct3D 9 graphics

The largest phase.

- **Device.** Replace DirectDraw enumeration, cooperative levels and flipping
  in `Screen.c`/`D3DMode.c` with `Direct3DCreate9` → `CreateDevice` and
  `D3DPRESENT_PARAMETERS`. Budget real time for **device-lost/reset
  handling** — a failure mode DX6 code has no concept of, which forces
  tracking of which resources must be recreated on reset.
- **Textures.** Rewrite `DX6TexMan.c`, `TexD3D.c` and `Surface.c` around
  `IDirect3DTexture9` in the managed pool, which also deletes the old texture
  paging logic.
- **3D draw path.** `D3DRender.c` maps fairly directly:
  `D3DRENDERSTATE_*` → `D3DRS_*`, texture stage states carry over,
  `D3DTLVERTEX`-style formats become FVFs, and `DrawPrimitive` becomes
  `DrawPrimitiveUP` first (optimise to dynamic vertex buffers later).
  Fixed-function only; no shaders are needed for parity.
- **2D path.** UI blits, fonts and cursor (`TextDraw.c`, `Font.c`,
  `BitImage.c`, `Cursor.c`, `Disp2D.c`) move from surface `Blt` to textured
  quads or `ID3DXSprite`; fonts via `ID3DXFont` or a baked glyph atlas.
- **Video.** `Sequence.c` decodes into a dynamic texture drawn as a quad; its
  software and 3dfx paths go away. See also Phase 6 — the decoder itself is
  being replaced.

Milestones, in order: clear + present → menus/UI → 3D world → a visual parity
checklist (transparency, additive effects, fog, terrain).

## Phase 3 — DirectInput 7 → 8

Small and mechanical: `DIRECTINPUT_VERSION=0x0800`, `DirectInput8Create`,
updated interface names in `DXInput.c`, link `dinput8.lib`. An afternoon, not a
project.

## Phase 4 — Audio: XAudio2, dropping QMixer and CD audio

**Revised.** The earlier plan kept the option of retaining QMixer; that option
is dropped. Audio moves to **XAudio2**, and QMixer and the CD audio features
are removed outright.

This is well contained because `TrackLib.h` is already a clean ~80-line
interface: `QSTrack.c` (849 lines) is simply its QMixer implementation. The
port replaces that implementation behind the existing interface.

- **Replace** `QSTrack.c` with an XAudio2 backend implementing `TrackLib.h`:
  a mastering voice plus pooled source voices, with per-sample volume, pan and
  frequency mapped onto voice parameters. 3D positional audio, currently
  QMixer's `bScale3D` path, becomes X3DAudio or a simple attenuation model —
  whichever matches the existing behaviour more closely.
- **Remove** `QMixer.lib`, `QMixer.dll`, `QMIXER.H` and the QSound references
  in `Aud.c`. Drop `EAX.H`, which is vestigial.
- **Remove CD audio.** `CDAudio.c` (230 lines) and its MCI usage go; in-game
  music is served from files on disk instead. `CDSpan.c` (631 lines) is
  related but distinct — it locates game data across multiple CDs. It should
  be removed too if game data is installed to disk, but that is a **decision
  to confirm** rather than an assumption; `cdspan_*` calls are referenced from
  game code and need unpicking.
- Review `Audio.c` (1289 lines), `Track.c` (703) and `Mixer.c` (283) for
  QMixer-shaped assumptions leaking above the `TrackLib.h` line.

DirectSound itself is unaffected by the D3D9 work, so this phase is severable
and can run in parallel with Phase 2.

## Phase 5 — Networking: custom UDP transport on WinSock2

**Revised.** DirectPlay 4 is removed entirely and replaced with a **custom
UDP-based transport layer implemented on WinSock2**. Porting to DirectPlay 8
is explicitly rejected — it would be nearly a full rewrite into another
deprecated API.

- Define a small transport interface covering what `NetPlay.c` / `NetSupp.c`
  actually need: session create/enumerate/join, player add/remove, and
  reliable plus unreliable sends.
- Implement it over WinSock2 UDP. Since the game's message layer already
  frames its own `NETMSG` payloads, the transport needs sequencing,
  acknowledgement and retransmission only for the traffic that requires
  reliable, ordered delivery; lockstep-critical game commands must keep the
  ordering guarantees DirectPlay provided today. Session discovery becomes LAN
  broadcast plus direct connect by address.
- **Remove** `dplayx.lib`, `dplay.h`/`dplobby.h`, and rewrite `NetProv.c`
  (service provider enumeration) and `NetLobby.c` (lobby launch), which are
  DirectPlay-shaped and largely disappear.
- `NetAudio.c` (voice chat) is dropped rather than ported.
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

- Delete `MPDPXtra.c` (751 lines), `MPDPXtra.h` (662 lines) and `MPlayer.c`
  (86 lines), and drop them from `Outpost.vcxproj`.
- Remove `Mplayer.lib` from `AdditionalDependencies`, and the Mplayer registry
  probing and lobby hooks in `NetLobby.c`.
- Low risk: the coupling is limited to `MPlayer.c`, which handles stat
  submission to the service.
- Naturally sequenced with Phase 5, since both concern DirectPlay-era
  networking.

### WINSTR.LIB — the FMV video codec

**This is not a string library.** Despite the name, `WINSTR.LIB` (and
`GameData/winstr.dll`) is Eidos' video streaming library: 64 exports in the
`Movie_*`, `Alpha_*` and `Streamer_*` families, declared in `STREAMER.H`. It is
consumed by exactly one file, `NeuronCore/Sequence.c`, and it decodes the
game's `.rpl` movies — the briefings and research sequences under
`GameData/sequences/`. `GameData/Dec130.dll` appears to be the associated
decoder.

Removing it therefore means **replacing FMV playback**, not deleting a utility.
The assets cannot be decoded without a replacement, so this needs a decision:

1. **Re-encode and re-implement.** Convert the `.rpl` assets offline to a
   modern container, and play them back through Media Foundation (or a small
   bundled decoder) into the D3D9 dynamic texture that Phase 2 introduces.
   Preserves the sequences; costs an asset conversion pass plus a new playback
   path.
2. **Drop FMV.** Replace sequences with static screens or skip them. Cheapest,
   but loses game content.

Option 1 is the recommended default, but it is the user's call. Either way the
work is tightly coupled to Phase 2's rewrite of `Sequence.c`, so the two should
be scheduled together. Once done, remove `WINSTR.LIB`, `winstr.dll`,
`STREAMER.H` and `Dec130.dll`.

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

## Verification

There is no MSVC or Windows SDK in the Linux development container, so a real
build baseline is not available there. Instead, every translation unit is
syntax-checked with **mingw-w64** against a shadow copy of the tree, using the
include paths and preprocessor definitions taken from the `.vcxproj` files. The
shadow neutralises two Windows-only things that GCC cannot process: the two
MSVC inline-assembly sites (`Fractions.h`, `RendMode.c`, `PieDraw.c`) and
includes whose case does not match the real filename.

This is a **proxy, not MSVC**. GCC is stricter in some places, MSVC under
`/permissive` is more lenient in others, and the harness cannot link (QMixer,
Mplayer and WINSTR are 32-bit MSVC binaries). It reliably catches the portable
C++ issues, which is what Phase 1 is about, but a real `msbuild` remains the
final word.

Current state: **208/208 units syntax-clean as C**; 81 units with 851 errors
remaining as C++.
