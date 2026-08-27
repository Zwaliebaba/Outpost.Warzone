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

Measured at the head of the Phase 8 work. The figures in brackets are what
this table said when Phase 1 measured it; the fall is Phases 4 to 6 deleting
QMixer, CD audio, DirectPlay and Mplayer, and Phase 8 folding render files
into their neighbours. Header counts are whole-tree and include the vendored
`DX9/Include`, so Phase 8's four deletions against two additions barely move
them.

| | |
|---|---|
| Source files | 193 `.cpp` (was 206), 374 `.h` (was 378) |
| Translation units | 74 NeuronCore (was 85), 118 Outpost (was 121) |
| Toolset | MSVC v145, Win32 (x86) only |
| Projects | `NeuronCore` (engine static lib), `Outpost` (game exe), plus `NeuronClient`, `NeuronServer` and `NeuronCoreTest` from the client/server restructure |

**Re-measured 2026-08-17**, after the client-library split, the asset-pipeline
and palette work, and the script module rewrite:

| | |
|---|---|
| Translation units | 20 NeuronCore, 42 NeuronClient, 1 NeuronServer, 117 Outpost, 12 NeuronCoreTest |
| Toolset | MSVC v145; **x64 is the platform that ships** and the only one CI builds (2026-08-27). The x86 configurations still exist in the project files and are unmaintained (`Docs/X64Readiness.md`) |
| Generated parser code | **None.** All six MKS lex/yacc grammars are gone |
| Win32 build warnings | 12 (was 343) |

The legacy DirectX surface was **well contained**: roughly 271 COM call sites,
almost all of them in ~15 NeuronCore files, with game code in `Outpost/`
barely touching DirectX directly. That containment is what made the graphics
work tractable, and it held: Phase 2 touched 50 files, of which six are under
`Outpost/` and three of those are a one-line include or symbol rename. What
remains of the 271 call sites is DirectInput, DirectPlay and DirectSound —
Phases 3, 4 and 5.

Subsystems in use today:

- **Graphics** — **done, see Phase 2**, and being simplified onto the device
  in Phase 8. Direct3D 9 throughout: an `IDirect3DDevice9` owned by
  `Screen.cpp` and drawn through by `Render.cpp` and `TexMan.cpp`. Was
  DirectDraw 4 surfaces plus a Direct3D 6 immediate-mode device.
- **Input** — **done, see Phase 3.** DirectInput 8 (`DXInput.cpp`,
  `DIRECTINPUT_VERSION=0x0800`).
- **Audio** — **done, see Phase 4**, and modernised in Phase 9: XAudio2
  behind `Neuron::AudioMixer`, the track/sample lifecycle in
  `Neuron::AudioSystem`, WAV decoding in `WavData.cpp`, and in-game music
  served from disk by `Music.cpp`. QMixer and CD audio are gone, and so are
  the QMixer-shaped C layers that sat above the backend — the `audio_*` and
  `sound_*` free functions with them.
- **Video** — **done, see Phase 6.** `MovieStream.cpp` decodes H.264/AAC in MP4
  through Media Foundation, with the soundtrack on the game's XAudio2 graph.
  Was `Sequence.cpp` streaming `.rpl` movies through `WINSTR.LIB` into a
  DirectSound ring buffer.
- **Network** — **done, see Phase 5.** QUIC via MsQuic (`Transport.cpp`,
  `HostCertificate.cpp`, `NetPlay.cpp`, `NetSupp.cpp`, `NetUsers.cpp`).
  DirectPlay 4 and the Mplayer.com matchmaking are gone.

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

All three held. The audit's answer is written up under Phase 2; the short
version is that the display is 32 bit and three software-composited buffers
stay 16 bit, pinned at RGB565 rather than negotiated.

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

`CINTERFACE` was defined for both projects, which kept the roughly 270 legacy
`lpVtbl` COM call sites compiling untouched. They were rewritten against
Direct3D 9 in Phase 2 rather than converted twice. It has since been removed —
see Phase 2 for what that exposed, which was 13 call sites, all outside the
graphics code.

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

**Status: done.** Both Win32 configurations build and link, and the game runs
on Direct3D 9: menus, a campaign level's 3D world, the HUD, transparency and
the backdrop all render. The milestones the phase set itself — clear +
present, menus/UI, 3D world — are met.

DirectDraw is gone from the tree. `ddraw.lib` is off the link line and
`d3d9.lib` is on it; no source outside the `DISP2D` block (see below) includes
`ddraw.h` or `d3d.h`.

### What each part became

- **Device.** `Screen.cpp` now owns an `IDirect3D9` and an
  `IDirect3DDevice9`. Enumeration, cooperative levels, the front/back surface
  pair and the flip collapsed into one `CreateDevice` and one `Present`;
  driver selection tries HAL with hardware vertex processing, then HAL with
  software, then the reference rasteriser. `D3DRender.cpp` no longer creates a
  device of its own — it borrows the framework's, which is what deleted its
  driver enumeration, viewport object, material and z-buffer surface code and
  took the file from 1624 lines to 582.

  The swap effect is `D3DSWAPEFFECT_COPY`, not `DISCARD`. It has to be:
  `pie_ScreenFlip(CLEAR_OFF)` means "show this and keep it", which the loading
  screen and the video loop both rely on, and which the old front-from-back
  `Blt` gave them for free.

- **Device loss** is handled where the plan expected it to cost time, and it
  did. `screenTestDeviceState`/`screenResetDevice` wrap
  `TestCooperativeLevel`/`Reset`, and `D3DTestCooperativeLevel` — the same
  entry point `WinMain` already called — resets, re-applies every render
  state, rebinds the texture pages and calls `pie_ResetStates`. The subtle
  part was not the reset: it was that the renderer caches "the state I last
  sent", so after a reset that cache is a lie. It is explicitly invalidated,
  or the first frame back would draw with default blend states.

  **Phase 8 removed the reason.** There were two caches of that state, and
  the explicit invalidation (`g_bStateCacheStale`) existed only to reconcile
  the second one. Stage B1a deleted the inner cache, so `pie_ResetStates` —
  which this path already called — is the single invalidation point, and the
  staleness flag is gone.

- **Textures.** The whole video-memory negotiation went. Direct3D 6 asked
  DirectDraw how much texture memory was free and chose between 32 full size
  pages, a mixed 256/128 layout and an 8-bit palettised mode, then uploaded
  each texture through a system memory staging surface and a `Blt`. The
  managed pool does exactly that job, so `TexMan.cpp` (was `DX6TexMan.cpp`) is
  32 managed 256x256 `A8R8G8B8` textures and an upload that locks, expands
  through a palette and unlocks — 942 lines down to 351.

- **Transparency.** Direct3D 9 has no colour keying at all, so the choice the
  old code made per card between the alpha test and DirectDraw's source colour
  key is now always the alpha test. Palette entry 0 gets an alpha of zero when
  a texture page is packed, which is the same pixels the colour key on black
  used to discard.

- **3D draw path.** As predicted, mostly a rename: `D3DRENDERSTATE_*` →
  `D3DRS_*`, the texture stage states carry over, and `D3DTLVERTEX` is
  re-declared with the same layout in `D3D9Vertex.h` with an FVF beside it, so
  the several hundred vertex-filling lines in `PieDraw.cpp` and `PieFunc.cpp`
  did not move. Two things did need care: `DrawPrimitiveUP` takes a
  **primitive** count where `DrawPrimitive` took a vertex count, and texture
  filtering moved from the texture stage to the sampler.

- **2D path.** The plan expected textured quads or `ID3DXSprite`. That turned
  out to be the wrong shape of answer, because the live UI already draws as
  textured quads through the pie layer — `iV_DrawImage`, `pie_DrawImage` and
  the iVis fonts in `TextDraw.cpp` never touched a surface. What actually used
  DirectDraw surfaces was a set of modules with no callers left, and the
  handful of places that write pixels straight into the back buffer.

  So: `Cursor.cpp` and `TexD3D.cpp` are deleted (below), and the pixel-poking
  paths — the backdrop, the FMV frames, the text over them, and the `DISP2D`
  debug display's line and blit primitives — go through a lockable back buffer
  instead. `screenLockBackBuffer`/`screenUnlockBackBuffer` is the whole of that
  interface. `ID3DXFont` and `ID3DXSprite` were not needed, and `d3dx9.lib` is
  not linked.

- **Surfaces.** `Surface.cpp`'s offscreen surfaces have no Direct3D 9
  equivalent that suits a colour-keyed CPU blit into the back buffer, so they
  are now plain 32-bit system memory images and the blits in `Screen.cpp` copy
  them by hand. `surfRecreate` became a no-op that says so: system memory
  cannot be lost by a mode change.

- **Video.** `Sequence.cpp` keeps its `WINSTR.LIB` decoder and its local
  buffer, and blits into the locked back buffer converting as it goes. A
  dynamic texture and a quad would be the right shape once Phase 6 replaces
  the decoder; doing it now would mean rewriting the same function twice.

### The 16-bit audit

Phase 0 asked for the 16-bit assumptions to be audited. The answer is that
they divide cleanly in two.

The display is 32-bit `X8R8G8B8` and everything the renderer draws is. But
three buffers are still composited in software as 16-bit — the backdrop, the
FMV frames, and the subtitle overlay on them — and there is no reason to widen
them: the FMV decoder emits 16-bit, and the backdrop is a fixed 640x480 asset.
So they are pinned at **RGB565** and converted on the way into the back
buffer, by the two inline functions at the top of `Screen.h`.

That pinning is what deleted the most repetitive code in the phase. Five
places — `pal_Make16BitPalette`, `bufferTo16Bit`, `dtm_Build16BitTexturePalette`
and `seq_SetSequence`'s two copies — each opened with the same page of loops
scanning `DDPIXELFORMAT` bit masks to discover what layout the card had handed
back. Nobody hands anything back now. All five are constants.

`DDPIXELFORMAT` itself is replaced by `SCREEN_PIXELFORMAT`, which carries the
bit count and masks and nothing else, for the code that still packs colours by
hand.

### What `CINTERFACE` cost to remove

Phase 1 leaned on `CINTERFACE` to carry the COM call sites into C++ untouched,
on the understanding that Phase 2 would rewrite them. It is now off for both
projects, because the D3D9 code is written as `device->Method(...)`.

Turning it off also exposes the DirectInput, DirectPlay and DirectSound call
sites, which are not this phase's business. There were **13** of them, in
`DXInput.cpp`, `NetProv.cpp` and `NetAudio.cpp`, and converting them was
mechanical. `REFGUID` was not affected — it keys off `__cplusplus`, not
`CINTERFACE`, and Phase 1 had already dealt with it.

### Modules that turned out to be dead

Four things the plan listed as work to do had no callers at all, and were
deleted rather than ported:

- **`Cursor.cpp`/`Cursor.h`.** A second thread blitting a cursor bitmap to the
  DirectDraw *front* buffer. Nothing has called `cursorInitialise` in a long
  time; the live pointer is `iV_DrawMousePointer`, which is a textured quad.
  It could not have been ported as written — Direct3D 9 has no front buffer
  blit, and the threading model is not one a device supports.
- **`TexD3D.cpp`/`TexD3D.h`.** `D3DTexCreateFromIvisTex` and friends,
  reachable from nothing. `d3dLoadTextureSurf` was declared and never even
  defined.
- **`Font.cpp`'s printing.** The fixed and proportional bitmap fonts. Ported
  anyway, since the module is small and still compiles, but its only caller is
  a commented-out line in `HCI.cpp`.
- **`D3DEnableFog`/`D3DSetFogColour`.** Direct3D's fixed-function fog. The
  game's fog is the per-vertex specular colour `pie_AddFogandMist` writes plus
  the clear colour, so these would have fought it. Gone rather than ported.

`SetD3DFlags`, `D3DSetClipWindow`, `D3DSetDepthBuffer`, `D3DSetAlphaKey` and
`d3d_bHardware` went the same way. `screenReInit` went too, and that one was
worth removing rather than leaving: it destroyed and recreated the device,
which would have left every texture page dangling.

### Two things renamed

`DX6TexMan.*` → `TexMan.*` and `Dderror.*` → `DXError.*`, with
`DDErrorToString` becoming `DXErrorToString`. Both names were wrong the moment
the code underneath them changed. The error table shrank from 367 lines to 98,
because most of what DirectDraw could fail at — surface management,
cooperative levels, colour keys, palettes — no longer exists.

### Left alone: the DISP2D debug display

`Outpost/Disp2D.cpp` and `Outpost/Edit2D.cpp` are a debug 2D map view and map
editor, entirely inside `#ifdef DISP2D`, which neither project defines. They
are not compiled and were not ported: they still call `psBack->lpVtbl->Lock`
and friends directly.

The `screen*` 2D primitives they use — `screenDrawLine`, `screenFillRect`,
`screenBlit`, `screenTextOut`, `screenDrawEllipse` and the rest — **were**
ported, and work, so the gap is only the direct DirectDraw calls in those two
files. Anyone re-enabling `DISP2D` has that to finish, or those files to
delete. Deleting them would have meant touching `HCI.cpp`, `Loop.cpp`,
`Init.cpp` and `Display3D.cpp`, which is a larger decision than a graphics
migration should make on its own.

### One behaviour change

`seq_RenderOneFrame` started its blit at `lpSurface + borderY * lPitch`
counted in **words** while `lPitch` is in bytes, so it stepped twice as far
down the surface as it meant to. It is invisible at 640x480, where `borderY`
is zero, and wrong at every larger resolution. The rewritten loop steps by
rows.

### Still open

- **Vertex buffers.** Everything draws through `DrawPrimitiveUP`, as the plan
  intended for the first pass. Dynamic vertex buffers are the obvious next
  optimisation and nothing in the design blocks them. **Now tracked as Phase
  8 stage D2**, which Phase 8's single draw funnel is the precondition for.
- ~~**The device name settings.**~~ **Resolved by Phase 8 stage A3.**
  `war_SetDirectDrawDeviceName`, `pie_SetDirect3DDeviceName` and their
  getters are deleted, along with the `-D3D`, `-RGB` and `-REF` switches and
  the `renderMode` registry key. Phase 2 left them because the config format
  was not its to change; it was Phase 8's.
- **The lockable back buffer.** `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` costs
  something on some drivers. It can come off once the backdrop and the FMV
  frames draw as textured quads — which is Phase 6's natural moment, since the
  decoder is being replaced anyway.
- **A visual parity checklist.** Transparency, additive effects and terrain
  were confirmed by eye against a running campaign level. Fog was not: the
  game's fog is vertex specular, it is off by default, and it wants a
  side-by-side comparison rather than a glance.

## Phase 3 — DirectInput 7 → 8

**Done**, and it was what it looked like: `DIRECTINPUT_VERSION=0x0800`,
`DirectInput8Create`, `LPDIRECTINPUT8`/`LPDIRECTINPUTDEVICE8` in `DXInput.cpp`,
and `dinput8.lib` in place of `dinput.lib`.

Smaller than even that suggests, because the DirectInput surface is one mouse.
`DXInput.cpp` is 185 lines and uses four DirectInput names in total, nothing
else in either project touches `psDI` or `psDIMouse`, and there is no keyboard
or joystick device — `Input.cpp` takes the keyboard from window messages.

Three things made it a swap rather than a port. `DirectInput8Create` takes the
interface by IID where `DirectInputCreate` handed back a versioned object, but
that is one call site. `IDirectInput8::CreateDevice` returns an
`IDirectInputDevice8` directly, so unlike the 3-to-7 upgrades nothing has to be
queried for afterwards. And `DIMOUSESTATE`, `c_dfDIMouse`, `Acquire`,
`SetDataFormat`, `SetCooperativeLevel` and `GetDeviceState` are unchanged, so
the mouse code around them did not move at all.

The vendored `DX9/Include/dinput.h` already supports 0x0800 — it defaults to it
— and `DX9/Lib/dinput8.lib` was already there. The link was checked before CI
rather than after, by looking for the symbols in the import libraries:
`DirectInput8Create` and `c_dfDIMouse` are both in `dinput8.lib`, and
`IID_IDirectInput8A` and `GUID_SysMouse` are in `dxguid.lib`, which stays on
the link line. Nothing needed `dinput.lib` any more.

## Phase 4 — Audio: XAudio2, dropping QMixer and CD audio

**Done.** The earlier plan kept the option of retaining QMixer; that option was
dropped. Audio is on **XAudio2**, and QMixer and the CD audio features have
been removed outright.

`QSTrack.cpp` is replaced by `XA2Track.cpp` behind an unchanged `TrackLib.h`;
`QMIXER.H`, `QMixer.lib`, `QMixer.dll` and the vestigial `EAX.H` are gone, as
are `CDAudio.cpp` and `Mixer.cpp`. In-game music is served from files on disk
by `Music.cpp`, and both volume sliders now move the XAudio2 graph rather than
the Windows system mixer. `CDSpan.cpp` stays for this phase: removing it rests
on game data being installed to disk, which is a decision of its own. **That
decision has since been taken in Phase 6** — the FMV conversion installs the
movies to disk, so `CDSpan.cpp` goes with it.

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
  music is served from files on disk instead. `CDSpan.cpp` is related but
  distinct — it locates game data across multiple CDs. **Resolved in Phase 6:**
  it is removed, because the FMV conversion installs the movies to disk and FMV
  was its last real user. Two figures quoted here were wrong and are corrected
  in [Phase6Plan.md](Phase6Plan.md) — the file lives in `Outpost/`, not
  `NeuronCore/`, and it is 535 lines plus a 38-line header, with 19 `cdspan_*`
  call sites across seven files to unpick.
- Review `Audio.cpp` (1289 lines), `Track.cpp` (703) and `Mixer.cpp` (283) for
  QMixer-shaped assumptions leaking above the `TrackLib.h` line.

DirectSound itself is unaffected by the D3D9 work, so this phase is severable
and can run in parallel with Phase 2.

## Phase 5 — Networking: QUIC transport via MsQuic  *(Done)*

**Revised twice, then a third time in the doing.** DirectPlay 4 is removed
entirely. Porting to DirectPlay 8 was explicitly rejected — it would have been
nearly a full rewrite into another deprecated API. The replacement is **QUIC
via MsQuic**: a hand-written reliable-UDP protocol was the original plan and
was dropped because nothing in this environment can test one. QUIC supplies
reliable ordered streams, unreliable datagrams, connection timeouts and TLS
1.3, which is DirectPlay's feature set plus encryption.

This sets the floor at **Windows 11**, since MsQuic's Schannel backend needs
Windows 11 or Server 2022 for TLS 1.3 — superseding the Windows 10 floor
Phase 4 chose for XAudio 2.9.

**The third revision: there is no LAN discovery.** The plan called for UDP
broadcast; a responder and finder were written and then deleted unbuilt,
because the destination is a server-authoritative setup where a relay server
owns the connections and listing games is a query to a known server. Building
the broadcast version first would have been building something whose only
future was deletion.

### What it became

| Was | Is |
|---|---|
| `dplay.h`, `dplobby.h`, `dplayx.lib` | `Transport.h`, `msquic.lib` (NuGet) |
| `IDirectPlayX_Send` with `DPSEND_GUARANTEED` | a reliable QUIC stream |
| unguaranteed send | a QUIC DATAGRAM, above ~1440 bytes falling back to the stream |
| `DPID` | `NETPLAYERID` in `NetTypes.h` |
| `DPSYS_*` system messages | `Transport::Event`, drained by `NETeventHandler` |
| `DPSESSIONDESC2`'s `dwUser1..4` | `NETSESSION::adwFlags` |
| replicated per-player data | a `NET_PLAYERSTATS` broadcast |
| packet encryption in `NetCrypt.cpp` | TLS 1.3 on every byte |
| service-provider list (IPX, modem, serial, TCP/IP) | one entry and an address box |
| `NetProv.cpp`, `NetLobby.cpp`, `NetAudio.cpp`, `MPDPXtra.cpp`, `MPlayer.cpp` | deleted |

`NetSupp.cpp` survives at a third of its size: half of it was DirectPlay
session helpers, and the rest is the netplay log, which a dozen files use and
which is not networking. `NetCrypt.cpp` keeps three of its four jobs for the
same reason — only the packet cipher was the transport's to replace.

The `NetAdd`/`NetGet` message macros and the `NETMSG` layout are unchanged,
which kept the blast radius inside the transport.

### The platform: x64 ships, Win32 leaves CI (2026-08-27)

**By owner decision the game ships x64 and Win32 is out of the build.** The
workflow builds Debug and Release for x64 only, and both **block**: the x64
legs ran `continue-on-error` while the migration was in flight, and with no
second platform to fall back on there is nothing left for that flag to
protect. The x86 configurations remain in the `.vcxproj` files and in
`Outpost.slnx`, unmaintained.

This was earned rather than declared. x64 had been building and linking
warning-free in all four configurations for a while, and the run before the
switch showed Release green on both platforms with the unit tests passing —
so x64 was already doing everything Win32 was, with the tests to say so.

`tools/crosscheck.py` follows: its default target is x64, matching CI, and the
old `--x64` flag is now `--x86` for the unmaintained half. A pre-CI gate that
checks a platform nobody builds is worse than no gate, because it reads as
coverage.

What this closes: [AGENTS.md §3](../AGENTS.md)'s stop-and-report on adding an
x64 platform, which has been standing since Phase 6 removed the last 32-bit
binary. What it does not close is [X64Readiness.md](X64Readiness.md)'s **Watch**
list — those are still there, and one of them (`==` on two objects comparing
half a pointer each) is now the only known width defect left in the VM.

## Verification

`NetTest/` was a console harness CI ran in both configurations: two processes
over 127.0.0.1, one hosting and one joining, checking the certificate and
handshake, 120 reliable messages at nine sizes verified per byte and per
sequence number, datagrams, and that a broadcast never returns to its sender.
It passed first time. **This was the first phase whose result was actually run
rather than only compiled** — which mattered, because whether Schannel accepts
a self-signed certificate generated by `HostCertificate.cpp` is not a question
compiling can answer.

**The harness is gone** (2026-08-16), deleted with the client/server
restructure that added `NeuronClient`, `NeuronServer` and `NeuronCoreTest`, and
its steps are out of the CI workflow. That result stands as a record of the day
it ran and is no longer re-checked on each commit. **CI ran nothing at all
until 2026-08-27**, when it gained the `NeuronCoreTest` suite (below); that
still does not put bytes between two processes, which is the property NetTest
had and nothing has yet replaced. Whatever replaces it wants to keep the one property that made
NetTest worth having — it started real processes and put bytes between them,
which is the only way that certificate question gets answered.

Loss, duplication and reordering are **not** simulated: MsQuic's public API has
no way to inject them and loopback has none to observe. Ordering under loss is
QUIC's problem rather than this codebase's; the framing on top of it is ours,
and that is what the mixed-size burst tests.

### What it does not do

- **No way to find a game.** Hosting works; joining takes a typed address,
  which the browser then shows as its single entry. The browser fills properly
  when the relay server answers `Transport::FindSessions`.
- **No host migration.** The session ends when the host leaves, by decision
  rather than omission.
- **`bLobbyLaunched` is still there**, permanently `FALSE`. The lobby went with
  `NetLobby.cpp`, but a dozen branches across five front-end files still test
  it, and collapsing those is a dead-code sweep rather than a transport change.

Two rules in [AGENTS.md](../AGENTS.md) that this phase reached before that
document existed, both since resolved by owner decision. **R14** forbids new
third-party dependencies and a package manager, and MsQuic arrives as a NuGet
package: R14 now names it as its one sanctioned exception, with the reasoning
recorded there. And **§1**'s naming table: the transport shipped as
`nettrans_*` free functions matching the surrounding `NET*` code, and has since
been renamed to conform — `Transport` in `Transport.h` with static methods,
`HostCertificate` beside it, and the harness re-verifying both in CI.

The full record — the eight decisions, the four things the plan got wrong, and
what each step actually cost — is in [Phase5Plan.md](Phase5Plan.md).

## Phase 6 — Removing Mplayer.lib and WINSTR.LIB

**Done.** Two third-party libraries were listed here, unrelated to each other
despite sharing a heading. **`Mplayer.lib` went with Phase 5.** `WINSTR.LIB` was
*replaced* by the Media Foundation path, and **stage B6 has now removed it**
along with `dsound.lib`, `STREAMER.H`, the four `GameData` decoder DLLs, the
`MovieTest` reference decoder and `CDSpan.cpp`.

**No checked-in third-party library remains in the tree, and no third-party
binary in `GameData`.** What is left is DirectX, the Windows SDK, and MsQuic
under its sanctioned exception — which is the endpoint this phase set itself,
and which also removes the last constraint pinning the build to 32-bit.
Adding an x64 platform is still a stop-and-report under
[AGENTS.md §3](../AGENTS.md), and still wants the `UDWORD`-holds-a-pointer
audit in the save-game fixup first.

B6 also re-enabled SafeSEH on the shipping executable:
`ImageHasSafeExceptionHandlers=false` was on both link lines only because a
1997 import library had no safe exception handler.

The measured state of the assets, the staged plan and the four decisions the
phase is gated on are in [Phase6Plan.md](Phase6Plan.md). Two findings there
change the shape of this section and are corrected below: the audio is not
carried the way this document assumed, and 164 of the 181 movies the game
references are not in the repository at all.

### Mplayer.lib — dead matchmaking service

**Done, in Phase 5.** `Mplayer.lib` was the Mplayer DirectPlay Extras library
from Mpath Interactive (© 1996-97), for the Mplayer.com online gaming service,
which shut down in 2001. As predicted, it was naturally sequenced with Phase 5,
since both concerned DirectPlay-era networking, and it landed there rather than
here.

`MPDPXtra.cpp`/`.h`, `MPlayer.cpp`, `NetLobby.cpp` and `NeuronCore/Mplayer.lib`
are deleted; `Mplayer.lib` and `dplayx.lib` are off both link lines; the
`GameData/multiplay/mplaynow/` payload is gone. No Mplayer reference remains
anywhere in the tree. Nothing is left of this item for Phase 6.

### WINSTR.LIB — the FMV video codec

**Done — the sequences play and the library is gone.**

**This is not a string library.** Despite the name, `WINSTR.LIB` (and
`GameData/winstr.dll`) is Eidos' video streaming library: 64 exports in the
`Movie_*`, `Alpha_*` and `Streamer_*` families, declared in `STREAMER.H`. It was
consumed by exactly one file, `NeuronCore/Sequence.cpp`, and it decoded the
game's `.rpl` movies — the briefings and research sequences under
`GameData/sequences/`. `GameData/Dec130.dll` is the associated decoder.
It turned out to be an **import library for `winstr.dll`**, not a static one.

Removing it therefore meant **replacing FMV playback**, not deleting a utility.
What that took:

- The `.rpl` assets were re-encoded offline to **H.264/AAC in MP4** and the
  `.rpl` files removed. 179 movies now ship: 19 converted from the original
  assets through a reference decode, and 160 from an OGG set that covered the
  campaign movies which were only ever on the CDs.
- `NeuronCore/MovieStream.cpp` decodes them through **`IMFSourceReader`** to
  `MFVideoFormat_RGB32`, which is the back buffer's own format — so the FMV
  blit no longer converts pixels at all.
- The soundtrack is an **XAudio2 voice on the game's own graph**, reached
  through a new `sound_GetEngine()`. The old module built itself a private
  DirectSound object because QMixer's had gone; the movie now obeys the game's
  volume. **Nothing in the tree includes `<dsound.h>` any more.**
- `Sequence.h` is unchanged and `Sequence.cpp` fell from 840 lines to 273.

Two things worth carrying forward. `SeqDisp.cpp` **did** have to change, against
the plan's expectation — it probes the movie file before opening it and globs
the sequences directory to decide whether video is installed at all, so a name
translation hidden below it silently disables every sequence. And the old
decoder scaled 320x240 movies to fill the 640x480 playback area via
`DFLAG_DOUBLED`, which nothing had recorded; Media Foundation has no equivalent
and the blit had to learn to do it.

#### The assets

19 sequences ship in `GameData/sequences/`, **15.7 MB** and 77 seconds in
total, the largest 1.4 MB. All are `ESCAPE 2.0` format 130, 16 bpp, 25 fps, at
320x240 or 192x168.

**Corrected: they do not all carry audio, and the ones that do not are why
`GameData/sequenceAudio/` exists.** Eight of the nineteen have an embedded
22050 Hz mono 4-bit track, which is what `Sequence.cpp` reads
`Movie_GetSoundChannels`, `Movie_GetSoundPrecision` and `Movie_GetSoundRate`
for before feeding a DirectSound buffer. The other eleven are silent and take
their audio from a `.wav` in `sequenceAudio/`, named alongside the movie in
`GameData/messages/*.txt` and played through `audio_PlayStream` — already
XAudio2 since Phase 4. No movie uses both paths.

That distinction constrains the replacement. The external wav is the sequence's
clock, not the video: `NP2.WAV` runs 13.5 seconds against a 0.96-second movie
that loops under it, and the sequence ends on the audio callback. So only the
eight embedded tracks need carrying and sync; muxing the external ones would
change what the sequence is. `sequenceAudio/` also holds the subtitle `.txt`
and `.txa` files, which is most of its file count.

The other correction is scale. The 19 shipped movies are the hard-disk subset;
the game references **181** distinct `.rpl` names, and the remaining 164 — every
`cam1\`, `cam2\` and `cam3\` briefing — are read from the CD via
`cdspan_GetCDLetter`. Conversion is therefore also where Phase 4's deferred
question about `CDSpan.cpp` gets settled.

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

**Settled.** No decoder ships with the game — Media Foundation's built-in
H.264 and AAC support is relied on, which is the only choice consistent with
[AGENTS.md](../AGENTS.md) R14 and with this phase's own stated endpoint of
system libraries only. The install requirement that follows is Windows N
editions needing the Media Feature Pack, and that is a legible fatal error at
init rather than a black screen. The original `.rpl` files are **not** kept in
the repository: the converter and a hash manifest replace them, and the
converted movies take their place in `GameData/sequences/`. The reasoning for
all four decisions is in [Phase6Plan.md](Phase6Plan.md#decisions).

Dropping FMV altogether — static screens in place of the sequences — remains
the cheap fallback if the conversion proves not to be worth it, at the cost of
game content.

Either way the work is tightly coupled to Phase 2's rewrite of `Sequence.cpp`,
so the two should be scheduled together. Once done, remove `WINSTR.LIB`,
`winstr.dll`, `STREAMER.H`, `Dec130.dll` and `CDSpan.cpp`. `dsound.lib` goes
too: `Sequence.cpp` is the last DirectSound user in the tree.

The last of the CD audio goes with `CDSpan.cpp` — `cdspan_PlayInGameAudio` and
the four `*CDAudio` script functions, none of which any script calls, with one
exception. **`playCDAudio` is not CD audio**: Phase 4 rewired it to
`music_PlayTrack`, five campaign and tutorial `.slo` files call it, and since
scripts are compiled at load an unknown name is a load failure. It stays.

**Phase 6 modernises the code it rewrites** rather than deferring it to Phase 7
— `Sequence.cpp` and `SeqDisp.cpp` only, a scoped exception to
[AGENTS.md §4](../AGENTS.md), since writing 1998-shaped code into files being
rebuilt is exactly the churn Phase 7 exists to avoid. The `seq_*` names and
`ConformanceMode` are left for Phase 7 on purpose.

After Phases 4-6 the only remaining non-system dependencies are the DirectX
libraries themselves — which also removes the last constraint pinning the build
to 32-bit, making an x64 target viable. Concretely: `NeuronCore/WINSTR.LIB` and
`NeuronCore/Mplayer.lib` are the last checked-in 32-bit static libraries and
`GameData/`'s four decoder DLLs the last 32-bit binaries. Phase 6 deletes all
six but does **not** add the platform — [AGENTS.md §3](../AGENTS.md) makes that
a stop-and-report, and it needs the `UDWORD`-holds-a-pointer audit in the
save-game fixup first.

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

## Phase 8 — Native Direct3D 9: retiring the iVis/pie layer

**Planned, and its four gating decisions are settled by owner decision**
(2026-08-15) — the full call-site rename, dropping the dead config keys,
attempting the clipper replacement behind a parity gate, and the ordering
against Phase 6. The record is in
[Phase8Plan.md](Phase8Plan.md#decisions--settled).

Phase 2 rewrote what the pie layer *talks to*; this phase removes
the layer itself. The `pie_*`/`iV_*` code was iVis's abstraction over five
renderers — software DDX, Glide, PlayStation, Direct3D 6 RGB and HAL — and
since Phase 2 exactly one backend exists, so the layer now dispatches through
function-pointer tables whose every slot is a no-op or null, keeps two render
state caches that `D3DReInit` has to reconcile after a device reset, converts
`PIEVERTEX` to `D3DTLVERTEX` on every draw, and carries a renderer-selection
configuration (`WAR_REND_MODE`, `-D3D`/`-RGB`/`-REF`, device-name strings, a
commented-out Software/Glide/OpenGL menu) that selects nothing.

**Stage A is done.** The five sub-stages — the dispatch tables and their
stubs, the dead draw paths, the renderer-selection configuration, the
empty-bodied functions, and the iVis surface residue — removed **3,024 lines
against 85 insertions across 55 files**, taking the fourteen layer
translation units from 6,185 lines to 4,573. That is 26% of the layer gone
before any restructuring, and more than the ~2,400 lines the plan estimated.
`tools/crosscheck.py` is clean in both configurations at 198/198 units, the
same count as the pre-change baseline, and `tools/check_case.py` passes.
**Built and linked clean under MSVC**, Debug and Release Win32, by CI on
[PR #6](https://github.com/Zwaliebaba/Outpost.Warzone/pull/6). **It has not
been run** — no Windows toolchain exists in the development container — so
the visual checklist in the plan is outstanding for the whole stage.

**Stage B is done too.** Collapsing the funnels removed a further 933 lines
against 584 insertions and deleted three translation units — `PieState.cpp`
folded into `D3DRender.cpp`, `PieTexture.cpp` into `Tex.cpp`, and
`D3DMode.cpp` into `D3DRender.cpp` — taking `NeuronCore` from 78 project
entries to 75. The headline is that the renderer no longer keeps the same fact twice:
the translucency state and the texture-page binding each had a second cache
in the D3D layer, and the second copy is what forced the `g_bStateCacheStale`
machinery Phase 2 had to add for device reset. Both are single now, owned by
the code that talks to the device, and that machinery is gone. Init and
shutdown, previously spread over four functions in three files, are one of
each. The layer stands at **11 files and 3,885 lines**, against 14 and 6,185
before Stage A — 37% removed.

The measured inventory, the dead-code evidence, the target module layout and
the staged execution are in [Phase8Plan.md](Phase8Plan.md). The short version:
14 translation units and 6,185 lines make up the layer; ~2,400 lines were
estimated provably dead and went first, behaviour-preserving; the live remainder collapses
into a renderer that calls the device directly — `Render` (state + one vertex
funnel, from `D3DRender.cpp` + `PieState.cpp`), `RenderModel`, `Render2D`,
`TexMan` (absorbing `Tex.cpp`), with the fixed-point matrix stack, the
software clipper and the palette module kept and renamed. The `.pie`/IMD
*model format* and its loader are game data and are not touched.

This phase also absorbs two items Phase 2 left open: the dead device-name
settings (deleted in stage A3) and dynamic vertex buffers (stage D2, after
the draw funnel is singular). Stages A and B landed first because they avoid
Phase 6's contact surface; stage C's rename, which touches `Sequence.cpp`,
waited for Phase 6 to merge and is now under way.

**Stage C is done.** C1 deleted the three `#define iV_* pie_*` alias
tables and landed the canonical names at 630 call sites. C2 moved the render
files onto their target names — `D3DRender` → `Render`, `PieDraw` →
`RenderModel`, `PieBlitFunc` → `Render2D`, `PieMatrix` → `RenderMatrix`,
`PieClip` → `RenderClip`, `PiePalette` → `Palette` — as a pure rename:
`git mv` for history, then `#include` lines, project/`.filters` entries and
include guards.

C4 then took the `iV_` prefix off 51 functions — into `namespace Neuron`
rather than stripped, because 8 of the 87 candidate names collide with the
Win32 API and `iV_HeapAlloc`/`iV_HeapFree` are macros that would have
hijacked `kernel32`. The 30 macros strip to bare `SCREAMING_SNAKE`. C3
consolidated the four legacy type headers into `RenderTypes.h` and `Model.h`,
with the image structures joining `BitImage.h`, `iSurface` joining
`RendMode.h`, and the `pie_Draw*` declarations moving to a new
`RenderModel.h`; `Ivi.h`, `Ivi.cpp`, `IvisDef.h` and `PieDef.h` are gone.
Only the `pie_` prefix remains, and that is a phase of its own.

Five findings from stage C are worth carrying forward, all recorded in
[Phase8Plan.md](Phase8Plan.md): a blind alias rewrite can produce a
self-referential `#define` that silently shadows the real one; the Debug CI
configuration does not exercise `/SAFESEH`, so only Release catches that
class of linker regression; a prefix that looks decorative may be standing in
for a namespace, so **check rename targets against the platform headers
first**; a macro that expands to nothing never type-checks its arguments, so
code inside one rots unseen; and most of the tree's headers were never
self-contained, compiling only because a hub header happened to arrive first.

**Stage D is part done.** D3 settled the texel-offset switch the only way
Direct3D 9 allows — its sampling rules do not vary by device, so the half-texel
offset is right for all of them or none, and on is what every configuration
already ran. It is a constant now, and the switch, its config key and a
write-only flag are gone. D1 then split: **D1a** deleted 449 lines of
`RenderClip.cpp` that nothing reaches, an `iVertex` clipping family beside the
`PIEVERTEX` one the renderer actually uses, which stage A's sweep had missed
because two of its entry points are `extern` in the header and so read as used.
That is ungated work — unreachable code, deleted, no behaviour to compare —
and it leaves the clipper at 595 lines rather than the ~1,000 D1 was costed at.

**D1b and D2 are open, and D1b now has a cheap way to answer its own gate.**
`bClip` is a parameter rather than a constant, and seven live draw sites
already pass `FALSE` — handing raw screen-space vertices to the device and
letting it clip them at the viewport, which is precisely what D1 proposes to
do everywhere. So the parity screenshots can be taken by forcing `bClip` off
on the clipped paths, before any rewrite exists to be judged. The behind-camera
convention survives untouched: the sentinel is written to both coordinates at
once, so the funnel's test on `sy` alone catches every case, and widening it
would be a regression rather than a precondition — an earlier revision of this
section said the reverse and was wrong. D2's cost also moved: a
dynamic vertex buffer must be `D3DPOOL_DEFAULT`, which puts a new resource into
the device-loss path — the path stage B disturbed most and which has still not
been run — for a saving no hardware this runs on would notice. Both are
recorded in [Phase8Plan.md](Phase8Plan.md).

## Phase 9 — Audio: retiring the QMixer-shaped stack

**Done, stages A–F.** The module is `AudioSystem.cpp`, `AudioMixer.cpp` and
`WavData.cpp` in `namespace Neuron`, called as `AudioSystem::PlayTrack(...)`
from 52 files; the `audio_*` and `sound_*` free functions, `Audio.h`,
`TrackLib.h` and `NeuronCore/Aud.h` are all deleted. The game supplies an
`AudioWorld` provider from `Outpost/GameAudio.cpp` (was `Aud.cpp`), so the
engine library no longer links against game symbols — the one dependency
edge in the tree that pointed the wrong way. `Track.h` survives holding the
`TRACK` and `AUDIO_SAMPLE` wire types and nothing else. The mmio RIFF reader
is a `std::expected`-returning `WavData`, which took `winmm.lib` off both
link lines.

Stages A–E are green on MSVC CI in both configurations;
`tools/crosscheck.py` (now `-std=c++23`) is clean at 193/193 through stage
F. **Not run** — the listening pass is the outstanding verification. The
record of what came out differently is in
[Phase9Plan.md](Phase9Plan.md#what-was-built).

Phase 4 swapped the backend behind an interface it deliberately
did not change; this phase changes the interface. The `audio_*`/`sound_*`
double dispatch existed so backends could swap underneath a stable middle —
the swap is done, exactly one backend has existed since, and the layering is
now cost without purpose, the same finding Phase 8 made about the render
dispatch tables.

The measured state: 4,379 lines across twelve files, with 182 `audio_*` call
sites in 43 translation units above them. The analysis found a dead surface of
~20 functions and fields with zero callers (including a PSX `VagID` parameter
threaded through the track API and discarded at the bottom), a critical
section guarding lists that only one thread has touched since Phase 4 confined
the audio-thread boundary to the backend, and one genuine layering defect:
`NeuronCore/Aud.h` declares functions that `Outpost/Aud.cpp` defines, so the
engine library links against game symbols.

The target is a C++23 module in `namespace Neuron` — `AudioSystem` (samples,
tracks, gates, ducking), `AudioMixer` (the XAudio2 graph, RAII voice
lifetimes), `WavData` (`std::expected`-based decode over `std::span`, which
retires the tree's last winmm API use and takes `winmm.lib` off the link
line) — with the game handing in an `AudioWorld` provider at init, so the
dependency edge points the right way. Behaviour is pinned by the Phase 4
contract: the four fixed slots, the 3D pool and its distance-steal, the duck,
the same-sound gates, and the save-game track-hash round-trip in
`ScriptObj.cpp` all stay observable-identical. Six stages, A–F: dead-surface
sweep, de-locking, mid-layer collapse, the rewrite behind a shim header, the
upcall severed, then the tree-wide call-site rename last and gated on an owner
decision, after Phase 6 stage B6.

The full analysis — the dead-surface evidence, the constraint list, the
idiom-by-idiom mapping, what is deliberately left unchanged, and the five
decisions to confirm — is in [Phase9Plan.md](Phase9Plan.md).

## Phase 10 — Renderer maths onto DirectXMath

**Under way; its six gating decisions are settled by owner decision**
(2026-08-16). Two rulings went beyond the plan's recommendation and widened
the scope: `NeuronCore/Trig.cpp` is in the phase, and the angle units stored
in game state migrate to float radians. The record and the staged execution
are in [Phase10Plan.md](Phase10Plan.md). **Stage A is done** — the
dead-maths sweep removed 88 lines from `RenderMatrix.cpp`/`.h`
(`pie_MatCreate`, `pie_VectorInverseRotate0`, the `pie_INVTRANS*` and
`pie_CLOCKWISE`/`X_INTERCEPT` macros, `pie_Clockwise`, and `pie_MatReset`
folded into `pie_MatInit`), behaviour-preserving, evidence greps recorded in
the phase plan. **Stage B is done** — the stack is an `XMMATRIX` stack
behind unchanged `pie_*` shims, the model-vertex loop and the IMD bounding
sphere are native DirectXMath, and Debug builds carry a fixed-point parity
shadow that reports the worst screen-space divergence at shutdown. What
came out differently is recorded in the phase plan: `BSPIMD.cpp` is dead
under an undefined feature macro and stays as found, `iIMDPoly::normal` is
write-only, `scaleMatrix` was a tenth direct matrix writer and became the
`pie_MatScale` shim, and the cross-check gained a `tools/stubs/directxmath.h`
transcription because mingw-w64's own header has no maths in it. **Stage C
is done** — all ~390 game-side sites compose `XMMATRIX` natively through
`Neuron::WorldMatrix`/`MatrixPush`/`MatrixPop`/`ProjectToScreen`, and every
`pie_*` maths shim died inside the stage with the sine table and its build
loop; `RenderMatrix` is 174 lines against the 522 the phase started from.
`PIEVECTORF` is `DirectX::XMFLOAT3`. Three latent defects were corrected
along the way (the effect-circle unsigned trig wrap, `scaleMatrix`'s
100% = 100.1%, the write-only poly normal), and the wrapped-negative-angle
conversion rule the stage established is recorded in the phase plan.
**Stage D is done** — `pie_MatInit` renamed to `Neuron::MatrixInit`, the
winding test moved beside its only callers in `RenderModel.cpp`, and
`Geo.h` folded into its fourteen includers and deleted. The renderer maths
migration is complete: `RenderMatrix` measures 166 lines against the 522
the phase started from, with no `pie_` maths symbol left in the tree.
**Stage E is nearly done** — the object and movement flip (E2) landed as
one coupled commit: `direction`/`pitch`/`roll`, the turret fields,
`sMove.dir` and the formation/drive state are float radians in (−π,π], the
trig API (`calcDirection`, `directionDiff`, the movement helpers) takes and
returns radians, and the level readers and net sync convert integer degrees
at the boundary. Five latent angle defects surfaced and were corrected
along the way (recorded in the phase plan). The camera flip (E3) followed:
`iView::r` is `XMFLOAT3` radians, WarCAM's spring-damper tracks radians
with `XMScalarModAngle` separations, the RayCast pitch helpers take and
return radians, and everything else that still bridged through
`RadiansPerWorldAngle` — effect tumble, sky shimmy, the animation
orientation chain, map markers — flipped with it, so the constant now has
zero users. Two more latent defects died in E3 (a negative average track
angle through a `UDWORD` into sin/cos, and a degrees-minus-radians
comparison E2 had left in the component renderer), and the provably dead
camera code went (`drawMapWorld`, `imdRot`/`imdRot2`, `disp3d_setView`/
`disp3d_getView` among others). The deletion (E4) closed the stage:
`NeuronCore/Trig.cpp` and its tables are gone with their `Window.cpp`
init/shutdown calls, and the `DEG` family, `RadiansPerWorldAngle` and the
legacy `PI` macro are deleted — the tree has no binary-angle or
degree-state symbol left in live code. **The phase is complete** (2026-08-16):
the build gates held green at every stage boundary (cross-check both
configurations, 188/188 units at the final head; MSVC CI stage by
stage), and the owner's Windows run —
[Verification.md](Verification.md#pass-i--phase-10-directxmath-and-the-radian-flip)
pass I — came back clean after two stage-F boot findings, both
boundary-conversion escapes of the same class (degree call sites that
still compiled against the radian APIs), were fixed and recorded in the
phase plan.

Phase 8 deliberately kept the fixed-point, pre-transformed-vertex pipeline
because changing it "is not simplification, it is a second project". This is
that project, scoped to the arithmetic only: the 4.12 `SDMATRIX` stack, the
5,120-entry sine table and the hand-rolled vector helpers in
`RenderMatrix.cpp`, `RenderModel.cpp`'s open-coded vertex transform,
`IMDLoad.cpp`'s double-precision bounding sphere and `BSPIMD.cpp`'s private
cross/normalise all move onto **DirectXMath** — `XMMATRIX`/`XMVECTOR`
computation composed natively at the call sites, `XMFLOAT3`/`XMFLOAT4X4` at
rest, no wrapper functions or classes. The pipeline architecture (CPU
transform, `D3DFVF_XYZRHW`, `DrawPrimitiveUP`, the software clipper) does not
change.

The measured surface is ~390 game-side call sites across 20 `Outpost/`
files — the hierarchical-transform idiom in `Display3D.cpp`, `Component.cpp`
and `Effects.cpp` above all — which is why the call sites are the migration:
keeping the `pie_Mat*` signatures would *be* the wrapper layer the phase
forbids. What survives as functions is renderer state and policy (the
matrix stack's push/pop, the world→screen projection, the geometric offset),
renamed per [AGENTS.md §1](../AGENTS.md). By the owner's rulings the phase
also retires `NeuronCore/Trig.cpp` (52 simulation call sites onto
`XMScalarSin`/`sqrtf`) and migrates the stored angle units — integer degrees
on every game object, binary angles in the camera, ~150 `DEG(` bridge sites
— to float radians, with the v≤8 level readers and the net wire keeping
integer degrees at the boundary. DirectXMath arrives from the Windows SDK —
header-only, nothing new under R14.

The key enabling fact, derived element by element in the plan: the
fixed-point rotations are exactly `XMMatrixRotationX/Y/Z` pre-multiplied
under DirectXMath's row-vector convention, with binary angles converted at
`2π/65536` — so the migration is mechanical substitution, verified by a
temporary dual-path parity check over a CAM_1A run rather than re-derivation.

## Removed outright: save/load and the demo (2026-08-16)

**By owner decision, the game no longer has user save games, and the demo-era
content is gone.** The game is heading to a server-authoritative MMO shape —
the direction Phase 5 set when it chose a relay-server world — and there a
local save of world state has no meaning. This landed as four changes, staged
so each was cross-checked green.

The line that made it safe runs at **format version 8**. The `.gam` container
is both the level format and the save format; measurement settled where one
ends and the other begins: every shipped level `.gam` is version 5-8 and every
shipped level `.bjo` (DInit, Struct, Feat) is version 8, while versions 9-33
existed only for user saves — the only v33 files in the tree were three
complete user saves checked into `GameData/savegame/`, now deleted. Everything
at or below the line stays loadable; everything above it is gone, readers
included, and the dispatchers reject it by version with a message saying why.

What went, in order: the UI and every path into it (the front-end Load button,
the esc-menu Load/Save, the mission-results Save/Load, `-savegame`, the
`GS_SAVEGAMELOAD`/`GAMECODE_LOADGAME` plumbing); the machinery (the writer
whole, `gameLoadV`, the full-state object readers, script-state save —
`EvntSave.cpp` leaves NeuronCore — and the FX/score/visibility pairs); the
format (the `SAVE_GAME_V10-V33` tower and 70-odd structs, deleted to a
fixpoint where no type's name appeared outside its own definition); and the
demo (68 dead `COVERMOUNT`/`MULTIDEMO`/`NON_INTERACT` gates, the E3 attract
camera, FastPlay — which existed only in its demo form — and the stale
`PROG`/`DEMO`/`MINIMAL`/`TEST` level entries, four of which referenced files
not in the tree).

The ten-slot requester survives as what it always also was: the multiplayer
force picker. The mission-results screen survives as the between-mission
Continue screen, with Quit offered directly where it used to be earned by
saving. `plotStructurePreview` — the lobby map preview, which reads
`struct.bjo` and looks like save code — survives narrowed to the shipped
layout.

**Three standing items changed shape:**

- **The x64 audit is off the books.** The stated precondition for an x64
  platform was "the `UDWORD`-holds-a-pointer audit in the save-game fixup";
  the fixup (`loadDroidSetPointers`/`loadStructSetPointers`) is deleted, so
  the audit has nothing to audit. Adding the platform remains a
  stop-and-report under [AGENTS.md §3](../AGENTS.md).
- **Phase 7's constraint** "any struct serialised into save files must stay
  byte-identical" becomes: *the shipped level formats (v≤8) must stay
  readable*. `NETPLAY` is no longer serialised anywhere, which is what let
  the `bLobbyLaunched` layout-hold field finally go.
- **Phase 9's save-game track-hash round-trip** constraint dissolves;
  `AudioSystem::TrackIdFromHash` lost its last caller with the script-state
  reader and is deleted.

One keymap subtlety was recorded in the code where it lived: the key-function
table's order was the id space saved keymap files indexed into, so
`kf_ToggleDemoMode`'s slot was refilled with a harmless function rather than
removed. The asset-pipeline work below has since dissolved it — `keymap.json`
stores each binding by function *name*, order no longer matters, and the
refill duplicate is gone.

None of this has been run — it is built-and-linked work like everything since
Phase 2, and [Verification.md](Verification.md) carries what a run must now
confirm: the campaign boot, the mission-results Continue flow, and the
multiplayer force picker are the paths this work touched most.

## The asset pipeline: WRF, stats tables and the audp grammar → JSON (2026-08-16)

**By owner decision, the data-description formats are JSON and their four
parsers are deleted.** The survey, the design and the full landed record live
in [AssetPipeline.md](AssetPipeline.md); whether this work takes a phase
number is still the owner's call (its decision 6), so this entry records it
without claiming one. Four stages, each pushed CI-green:

- **A — deletion and guard rails.** The WDG archive layer, the registrant-less
  file-load machinery, the dead loaders and the `.tag`/`.jbf` files went;
  `tools/validate_assets.py` joined CI and immediately caught real breaks
  (`vidmemC.wrf` referenced six texture pages that never existed — the Kevlar
  campaign would have fataled on load).
- **B — manifests.** `Neuron::Json` (hand-written strict RFC 8259, tests in
  `NeuronCoreTest`) and `GameData/datasets.json` replaced the WRF grammar,
  the `.lev` parser, the 84 `.wrf` files and `GameDesc.lev`;
  `Outpost/Manifest.cpp` replays a unit as the same resource-load calls the
  grammar actions made.
- **C — tables.** The 133 stats/message `.txt` tables became JSON, proven by
  token-level round-trip before the sources were deleted; the loaders fetch
  fields by name through `Outpost/StatsTable` (missing field = fatal, where
  `sscanf` zero-filled silently); array order still derives the `REF_*`
  numbers.
- **D — the small wins.** The `.ani` scripts, `anim.cfg` and the audio
  configs became JSON and the generated MKS `audp_` lex/yacc parser left the
  tree; binary `keymap.map` (invalidated by a build-time stamp on every
  rebuild) became `keymap.json` with named function bindings.

The constraint the save/load removal left — *the shipped level formats (v≤8)
stay readable* — is untouched: `.gam`/`.bjo` and the media formats were
explicitly out of scope. Like everything since Phase 2 this is
built-and-verified work, not run work; a campaign and a skirmish load are
what a Windows run must confirm.

## The display: desktop-resolution borderless window, scaled UI (2026-08-16)

**By owner decision, the fixed-resolution display is gone.** The game now
starts in a borderless `WS_POPUP` window covering the desktop at the
desktop's own resolution, presented through a windowed Direct3D 9 swap chain
— there is no exclusive full-screen mode, no Alt+Enter toggle, no `-640` …
`-1280` switches and no `resolution` registry key. `frameInitialise` makes
the process DPI-aware (per-monitor-V2 where the OS has it, resolved at run
time), reads the desktop metrics, and derives two sizes from them:

- **Physical**: `screenWidth`/`screenHeight` in `Screen.cpp` — the desktop,
  the back buffer, the viewport.
- **Logical**: the `pie_GetVideoBufferWidth/Height` canvas the game computes
  every coordinate on — the desktop divided by an integer **display scale**
  (`Neuron::DisplayScale`, stored beside the canvas in `RenderClip.cpp`).
  The scale is the largest whole number that keeps the canvas at least
  960x540, so the 640x480-anchored UI keeps roughly the same physical size
  whatever the pixel density (1080p and 1440p get 2, 4K gets 4).

The bridge between the two is deliberately thin, because everything drawn
goes through one of four narrow places: `D3DDrawPoly` multiplies every
pre-transformed vertex by the scale (models, terrain, HUD quads, text — one
funnel); `pie_RenderImageToBackBuffer` and `seq_RenderOneFrame` replicate
each FMV pixel into a scale-sized square; `drawBackDrop` does the same for
the backdrop; and `screen_Upload` samples every scale-th pixel reading the
back buffer back down to the logical canvas. Input mirrors it: the window
message handler divides the physical mouse position by the scale, so the
game, the widgets and 3D picking all stay in one coordinate space. The
640x480-relative UI layout Phase 8 chose to keep is untouched — it now lays
out on the logical canvas and arrives on screen scaled.

`pie_GetResScalingFactor` became a formula (`logicalWidth * 100 / 640`),
scaling the world off the width: the horizontal view span stays what the
640-wide layout was designed for and the vertical span follows the window's
aspect ratio, the safe direction while `VISIBLE_XTILES` is still a fixed
32-tile grid tuned for 4:3. Widening the vertical/horizontal trade for
widescreen is a possible follow-up, not part of this change.

**Follow-up, planned 2026-08-26:
[NativeResolutionPlan.md](NativeResolutionPlan.md).** The window is the
desktop; the *world* is not. Because the projection's focal length and centre
are both in logical units, a 4K desktop projects the world onto a 960x540 grid
— and the terrain mesh goes through `Neuron::ProjectToScreen`, which returns
`SDWORD`, so every landscape vertex is quantised to a whole logical pixel
while the models beside it project in float. That plan gives the world pass a
physical-resolution projection at an unchanged field of view and leaves the
interface on this canvas. It does not touch `VISIBLE_XTILES`, which is why it
is a renderer change rather than a widescreen one.

What this deliberately does not do: change resolution at runtime (the
canvas, `DisplayBuffer`, the widget root and the radar are all sized at
init), scale the CPU debug-2D paths in `Screen.cpp` (`screenTextOut` and
friends — reachable only from the `DISP2D` editor tree), or touch the dead
DirectInput mouse path (`DInpGetMouseState` has no callers). Device loss
handling stays exactly as Phase 2 built it; a windowed swap chain just makes
it rarer.

Like everything since Phase 2 this is built-and-verified work, not run work:
crosscheck is green, and [Verification.md](Verification.md) now carries what
a Windows run must confirm — the borderless boot at desktop resolution, UI
scale 2 on a 1080p/1440p desktop, mouse-to-widget alignment, an FMV with
subtitles, and a load/save-screen backdrop round trip.

## Removing the palette: true-colour assets in DDS (2026-08-16/17, complete)

**By owner decision, the 256-colour palette is to be removed, and the target
asset format is DDS** (uncompressed A8R8G8B8, hand-rolled loader — no D3DX,
no new dependency; the 128-byte header is trivial to read and to write from
a stdlib-only Python tool). The palette is live and load-bearing today:
`GameData/palette.bin` (256 RGB triplets) is the single global palette, the
73 PCX texture pages plus the `.img` UI pages store indices *into it* (the
PCX loader warns if a file so much as carries its own palette), and the
pixels stay 8-bit in memory until the last moment — `dtm_UploadImage`
expands them through `texPal32Bit` at device upload, with entry 0 as the
transparent colour-key. On top of the assets, a wide band of code treats
colour as a palette index: `pal_GetNearestColour` (~30 sites), the 16
`COL_*` primaries (106 sites across the widget library and game UI), the
index-taking draw calls (`pie_Line` 161 sites, `pie_BoxFillIndex` 50,
`pie_Box` 9), widget colour tables and font colour indices. Team colours
need nothing special: they are baked into palettised page variants selected
as texture frames in `pie_Draw3DShape`.

The removal is staged so every stage ships alone and the gate is visual
parity — the same RGB values fall out of each stage, so screenshots should
match to the pixel except where 256-colour quantisation disappears:

1. **Delete the provably dead half** — *landed with this entry.* The
   transparency lookup (`transLookup`, `pie_BuildSoftwareTransparency`),
   `palette16Bit`, the never-read FMV palette (`pVideoPalette` and its
   555 build loop — unread since the MP4 decoder), the Windows-palette copy
   (`psWinPal`, `pie_GetWinPal`, `screenSetPalette`, `screenGetPalEntry`
   and `asPalEntries` — last real consumer was the dead `DISP2D` editor
   tree), the per-page `TEXTUREPAGE.Palette` that was allocated and freed
   but never read, `pcxBufferTo16Bit`, `iPalette`, `gamePal`, `tempPal`,
   the empty `pal_Init`/`pal_SelectPalette`/`pal_SetPalette` and the
   `OLD_PALETTE` block. What survives is exactly the live half: `psGamePal`,
   `pal_GetNearestColour`, `palShades` (radar lighting), `palette32Bit`
   (FMV subtitle glyphs) and the `COL_*` machinery.
2. **True colour in memory, same assets** — *landed.* `iBitmap` is the
   packed A8R8G8B8 pixel now, which let the compiler enumerate the
   consumers. The PCX loader expands each index as it decodes (index 0 →
   alpha 0), `dtm_UploadImage` is a straight row copy (`texPal32Bit` and
   its builder gone), the radar composes in 32-bit — averaged tile colours,
   per-channel-multiply lighting replacing `palShades`, clan/flash tables
   carrying the packed values of the entries they used to index — and the
   backdrop is 32-bit end to end (`bufferTo16Bit` gone, `DisplayBuffer`
   sized ×4). The FMV subtitle glyphs read page pixels directly, deleting
   `palette32Bit`. The dead software-renderer intel-map fill went rather
   than being converted.
3. **Colour-as-index becomes packed RGB** — *landed.* `COL_*` are packed
   constants (the RGB values `pie_SetColourDefines` used to ask the palette
   for), `pie_Line`/`pie_Box`/`pie_BoxFillIndex` take packed colour as
   `pie_BoxFill` already did, the widget colour tables, bar graph colours,
   tool tip and text colours are packed (the negative `PIE_TEXT_*`
   sentinels became the colours they named — `-1` still reads as white,
   which is what "use the bitmap's own colours" meant), and every literal
   palette index at a draw site (score bars, radar arrows, the drag-box
   strobe ramp, health-bar backing) carries the packed value of the entry
   it used to name. `pal_GetNearestColour` and `pie_SetColourDefines` are
   deleted. The narrowing hazard was the real work: colour variables and
   casts of `UBYTE`/`UWORD` width would silently truncate packed values,
   so every one was found and widened by hand — the cross-checker cannot
   catch those.
4. **Convert the assets and delete the module** — *landed.*
   `tools/convert_pcx_to_dds.py` (stdlib-only, kept as the record of the
   conversion) expanded all 73 PCX files through `palette.bin` into
   uncompressed A8R8G8B8 DDS - index 0 → alpha 0, the same expansion the
   loader had been doing at run time, so the files now hold byte for byte
   what the game held in memory - retargeted the 177 `.pcx` references in
   `datasets.json`, and patched the fixed-width `TPageFiles` name tables in
   the two `.img` headers ("pcx"→"dds" is the same length). The 121 `.pie`
   model files needed nothing: the IMD loader normalises their TEXTURE
   names to extensionless `page-NN` keys, and their `pcx` type tag stays
   as a format token. `Dds.cpp` (~250 lines, `Neuron::DdsLoad` and the
   Mem/ToBuffer variants - a header validation and a copy) replaced
   `Pcx.cpp`; `Palette.cpp` and `palette.bin` are deleted, and `Palette.h`
   is down to the packed `COL_*` colour constants. Converting GameData
   binaries was sanctioned the way the Phase 6 `.rpl`→MP4 re-encode was:
   by owner decision, through the committed tool.

   One measured finding, for the record: every shipped PCX carries an
   embedded palette that differs from `palette.bin` by 3-4 bytes of 768 -
   rounding, plus disagreement about the RGB of the transparent entry 0,
   which never renders. The game always ignored the embedded palettes, so
   the conversion went through `palette.bin`, matching what the game drew.
   `tools/validate_assets.py` reports the identical 0 errors / 981
   warnings before and after the conversion.

## The script module: native compiler, x64-clean VM (2026-08-17, complete)

**By owner decision, the scripting module was rewritten with no lex/yacc —
native C++ only — and made x64-clean**, with the compiled form of a script
explicitly freed from backward compatibility and wider modernisation in
scope. The full survey, design and staging are in
[ScriptRewrite.md](ScriptRewrite.md); the language the new compiler accepts —
recovered from the generated parser and pinned by measuring all 182 shipped
script files — is specified in [ScriptLanguage.md](ScriptLanguage.md).

Why it mattered beyond tidiness: the script VM was the **only remaining x64
Blocker**. Its instruction stream was an array of 32-bit words with C
function pointers stored inline, which cannot work where a pointer is 8
bytes. Three generated grammars (`.slo`, `.vlo`, `STR_RES`) totalling 10,907
lines stood in front of it, generated by an MKS lex/yacc the project no
longer has, so the grammars could not be changed — only the generated output
hand-edited.

What landed, in six commits:

1. **Dead weight deleted** — `scriptSaveProg`/`scriptLoadProg`/
   `scriptGetVarIndex` (no callers since the save/load removal), every
   `#ifdef NOSCRIPT`, and the declaration-only script-function machinery.
2. **Native `.slo` compiler and a new encoding.** `ScriptLex` + `ScriptComp`
   replace `Script_l/_y`; one `ScriptInstr` record per instruction, callees
   held as table indices, jumps counted in instructions, `SCRIPT_CODE` owning
   its storage. `Interp.cpp` and `CodePrint.cpp` follow. Script-defined
   `function` blocks became a working feature (owner decision) — the old
   grammar reserved the keyword and compiled nothing.
3. **Native `.vlo` parser**, and `eventSetContextVar` typed on `INTERP_VAL`.
4. **Typed instinct FFI** (owner decision): `stackPopParams`'s varargs, which
   stored every parameter as 4 bytes through destinations that were often
   `DROID**`, became a typed interface whose store width is fixed by the
   destination. All 219 pop sites and every push site converted.
5. **Context values** moved from chunked linked lists to one `std::vector`
   per context; the `EVENT_INIT` pool tuning went with them.
6. **Native string-resource parser**, retiring the last generated grammar.

Verified: `check_case` and full `crosscheck` on every stage, both Win32 CI
configurations green, and a `NeuronCoreTest` suite covering the compiler,
interpreter and script functions. **The corpus acceptance test is a boot** —
the game compiles all 59 `.slo` and 123 `.vlo` at startup, so a campaign
level and a skirmish match are what prove the rewrite semantically.

Bugs fixed on the way through, none of them the point of a stage: context
copies truncating object pointers, four `ScriptAI.cpp` sites parking
pointers in `SDWORD` locals, `.vlo` object initialisation truncating, and
trigger labels never reaching the debug info (`eventGetTriggerID` printed
`NOT FOUND` for every code trigger).

## The hand-rolled containers: standard containers or deletion (2026-08-17, complete)

**By owner decision, `TREAP`, `QUEUE` and `PTRLIST` were to move onto
standard containers and their files deleted.** The survey found these are
not the same job — two of the three had no consumers left at all:

- **`PQueue.cpp`/`.h` (457 lines) had no consumers.** Nothing outside the
  module included `PQueue.h` or called a `queue_*` function; it was the
  A* pathfinder's queue once, and `AStar.cpp` has long had its own. There was
  nothing to migrate — it is a deletion with a grep proof.
- **`PtrList.cpp`/`.h` (323 lines) had no consumers either**, and could not
  even have linked if it had: it reads an `extern void* g_ElementToBeRemoved`
  that is defined nowhere in the tree. Its own guard — a
  `static CRITICAL_SECTION critSecAudio` around every mutation — names the
  owner it outlived: this was the QMixer sample list, and the audio stack
  went in Phases 4 and 9.
- **`Treap.cpp`/`.h`/`TreapInt.h` (588 lines) had exactly one consumer**, the
  string-resource system, so the real work was modernising `STR_RES` rather
  than swapping a container behind it.

With these gone, and `HashTabl` before them, **`NeuronCore` has no
hand-rolled container left** — which is what R10 was asking for: all four
carried their own node pools (`init`/`ext`/`elementSize` triples) behind a
container interface.

`STR_RES` was a treap keyed on the *address* of the ID string (with a
`strcmp` comparator — the idiom that had already forced the `TREAP_KEY`
pointer-width fix in `X64Readiness.md`), a chain of fixed-size `STR_BLOCK`s
walked linearly on every lookup, an `ID_ALLOC` flag bit separating ids the
system allocated from ids the game's keyword table owns, and hand-written
`stringLen`/`stringCpy`. It is now:

```cpp
std::vector<std::unique_ptr<STR_ENTRY>> aEntries;   // by id: keyword + text
std::unordered_map<std::string_view, UDWORD> idMap; // keyword -> id
```

Two caller requirements shaped that, both found by reading consumers rather
than assumed. The ~500 `strresGetString` sites store the returned `STRING*`
in widgets, view data and script string values for the session, and
`strresGetIDString` hands out the *stored* keyword for the same purpose — so
element addresses must never move, which is what the `unique_ptr` elements
buy. The map keys are `string_view`s into those stable entries, so a lookup
by `const char*` costs no allocation.

Everything else fell out: `ID_ALLOC` is gone because every entry owns both
halves, `strresDestroy` is a `delete`, the block walk is an index, and
`strresCreate` lost its pool-sizing parameters. `strresGetIDfromString`,
`stringLen` and `stringCpy` were deleted as unused. The public interface is
otherwise unchanged, so the 30 consumer files were untouched.

Also removes 2 of the build's 12 remaining warnings (C4715 in `treapFindRec`
and `treapDelRec`). `check_case` clean, `crosscheck` 181/181 units clean.

## Server authority: the MMO shape (2026-08-27, stages A and B landed)

**By owner decision the game is heading to a server-authoritative MMO, starting
with single player.** The design — where the tree already stands, why the 1998
peer-distributed model cannot be the destination, the embedded-server topology
and its separation ladder, the A–I staging and the full client/server message
protocol — is in [ServerAuthority.md](ServerAuthority.md); whether it takes a
phase number is still the owner's call (its decision 5), so this entry records
it without claiming one.

Two decisions are already taken. The server side ships **embedded inside
`Outpost.exe`** first, exchanging encoded messages with its own client through
an in-process transport, so that where the server runs is a launch-time binding
rather than a rewrite; and **single player is the first server-authoritative
session**, which puts the campaign, its scripts and every AI player on the
server side by construction. **Pause is removed as a feature** rather than
carried — an authority serving a session does not stop the world — with game
speed and the cheat console demoted to session commands a solo session permits
and a service session refuses.

**Stage A is done.** The simulation left the frame handler: `SimulateTick()` in
[Loop.cpp](../Outpost/Loop.cpp) is the whole of the world update as one
function, driven by `Neuron::ConsumeSimulationTick()` from
[GTime.h](../NeuronCore/GTime.h) at a fixed 40 ms quantum — 25 Hz, which is
`BASE_DEF_RATE`, the rate `Move.cpp` was written around and seeds its
frame-time history with. `gameTimeUpdate` now advances the *target* rather than
`gameTime`, and the leftover under a tick rolls forward instead of being
rounded away. The 219 moved lines are byte-identical apart from indentation.
Presentation moved off the simulation clock in the same change: `processEffects`,
`atmosUpdateSystem` and `processAVTile` run from the terrain draw and read
`frameTime2` now, or they would have animated at a fixed 40 ms per frame. One
latent defect died in the rewritten lines — an unsigned wrap in the owed-time
subtraction that read as a four-billion-tick backlog for the opening frames of
a level, and took `baseTime` with it.

`check_case` passes and `crosscheck` is clean at 180/180 units in all four
configurations. Like everything since Phase 2 this is built-and-verified work,
not run work: unchanged *speed* is not unchanged *smoothness*, and
[Verification.md](Verification.md) pass J carries the question — world motion
is 25 steps a second until the renderer interpolates between them.

**Stage B's first pass is done**, and it came with its own instrument:
`tools/crosscheck.py --sim-only` compiles the candidate server units with the
`NeuronClient` include directory taken away, so a unit that still compiles
reaches nothing presentational even transitively. It is a ratchet — `NeuronCore`
stays at zero, the Outpost count falls and never rises — and it went from
**22 of 58 client-free to 41 of 58**.

The measurement is the point. 105 of 117 Outpost units reached the same ten
client headers, because `ObjectDef.h` carried `RenderTypes.h` for its children
while using nothing from it, and `Base.h` embeds `SCREEN_DISP_DATA`, which
carried `IMD.h`. Almost every object-model use turned out to be a *pointer* —
`iIMDShape`, `ANIM_OBJECT`, `AUDIO_SAMPLE`, `W_SCREEN` are stored and passed by
the def headers and dereferenced by none of them — so ten headers now declare
what they point at instead of including the library that defines it. `iVector`
and `iPoint` are anonymous structs and could not be declared that way, so they
moved from the client's `RenderTypes.h` to `NeuronCore/Types.h`, the vocabulary
both halves share (`SDWORD` and `int32` are the same type, so no layout moved).
`ListMacs.h` — 70 lines of list macros with no includes — moved from
`NeuronClient` to `NeuronCore`, where a shared utility belongs. Seven includes
were simply stale. In exchange, 24 presentation units that had been getting
client headers transitively now include them directly.

What remains is real and countable: seventeen units in three groups — five
reading model geometry through `Model.h`, five playing sounds through
`AudioSystem.h`, and seven one-off client calls, of which `Order.cpp` reading
the keyboard through `keyDown` is the clearest layering defect in the tree.
That list is the input stage D's message planes consume, and it is recorded in
[ServerAuthority.md](ServerAuthority.md#b--split-simulation-state-from-presentation-state-first-pass-landed-2026-08-27).

## The model format: NMO, a CMO-derived binary mesh (2026-08-27)

**Design and tooling, plus the engine half; nothing draws one yet.** The
format is [NeuronMeshObject.md](NeuronMeshObject.md), the migration is
[PieToNmoMigration.md](PieToNmoMigration.md), and between them they answer a
question Phase 8 deliberately left shut: the `.pie`/IMD model format is still
untouched by that phase, but it is no longer *unexamined*.

NMO derives from Microsoft's CMO (the layout in DirectXTK's `CMO.h`), keeping
its `Material`, `Vertex`, `SkinningVertex`, `MeshExtents`, `Bone`, `Clip` and
`Keyframe` structures byte for byte, and adds what this engine turned out to
need: bone animation and named markers **per submesh** rather than only per
mesh, a container that can be identified, versioned and validated, and
material state for the two things the `.pie` renderer does per polygon per
frame — colour-key transparency and the texture atlas that is really the
player-colour table (1,543 of the 1,693 texture-animated polygons declare
eight frames because eight is the player count).

What is in the tree:

- `NeuronClient/Nmo.h` — the file layout in C++, 31 assertions on struct
  sizes and alignment, no logic. They hold on x86 and x64, so either build
  writes the same file.
- `NeuronClient/NmoLoad.cpp` — one read, validate, then views into the bytes.
  A malformed file is rejected with a reason, never repaired and never
  asserted on: `DEBUG_ASSERT_TEXT` routes to `Neuron::Fatal`, which would take
  a debug build down on a bad asset.
- `NeuronClientTest/NmoTest.cpp` — 35 tests: 9 read the golden model back, 26
  reject a copy corrupted at one field, one per clause of the validation list.
  The golden model is generated from the Python reference codec by
  `tools/make_nmo_fixture.py`, so the two implementations are tested on the
  same bytes rather than assumed to agree.
- `tools/pie_to_nmo.py` — converts all 516 shipped models into a mirrored
  tree, and all 516 load back through the C++ loader; `tools/blender_nmo/`
  imports and exports them so the format is editable.

**What it does not do: draw.** `GameData/` still holds `.pie`, the renderer
still calls `pie_Draw3DShape`, and the conversion writes a candidate tree
beside `GameData/` rather than into it. Stages D and E of the migration —
render behind a flag, look at the screen, then convert the data and retire the
`.pie` loader and the `.ani` system — need a Windows machine and a running
game. Until then this is the same built-and-not-run state as everything since
Phase 2, with the same caveat: 516 models converting and loading says nothing
about whether one of them appears the right way up.


## Verification

There is no MSVC or Windows SDK in the Linux development container, so a real
build baseline is not available there. Instead, every translation unit is
syntax-checked with **mingw-w64** against a shadow copy of the tree, using the
include paths and preprocessor definitions taken from the `.vcxproj` files.
`tools/crosscheck.py` is the harness. The shadow neutralises the Windows-only
things GCC cannot process: includes whose case does not match the real
filename, the Concurrency Runtime headers `NeuronCore.h` includes but never
uses, and — under `tools/stubs/` — declarations for the headers mingw-w64
does not ship at all: `x3daudio.h`, the MsQuic headers and `<winrt/base.h>`
(a `com_ptr` minimal enough for the Media Foundation code Phase 6 added).
Those last are a transcription
of somebody else's API and check our use of it rather than themselves; each
says so at the top. The inline-assembly handling has gone with the last `__asm` in the
tree, and so has the `CINTERFACE` define — nothing in the tree defines it any
more, and leaving it in the harness failed seven units against a green build.

This is a **proxy, not MSVC**. GCC is stricter in some places, MSVC under
`/permissive` is more lenient in others, and the harness cannot link (QMixer,
Mplayer and WINSTR are 32-bit MSVC binaries). It reliably catches the portable
C++ issues, which is what Phase 1 is about, but a real `msbuild` remains the
final word.

**CI runs the unit tests as of 2026-08-27.** `NeuronCoreTest` is not a
reference of `Outpost` — it compiles the `NeuronCore` sources it tests directly
— so building the game never built it, and the `Neuron::Json` and script
compiler/VM suites sat in the tree unbuilt and unrun. The workflow now restores,
builds and runs them through `vstest.console.exe` in all four configurations.
Two things had to change for that to be possible: the project carried the
pre-VS2017 `$(VCInstallDir)UnitTest\include` path for the native test
framework, which VS2017 moved under `Auxiliary\VS` (both are listed now, since
a directory that does not exist is ignored), and the project needed its own
NuGet restore. This is the first thing CI has *executed* since NetTest was
deleted.

Two content checkers run ahead of the build in CI: `tools/check_case.py`
(every include and project entry must match the on-disk filename case) and,
since the asset-pipeline work, `tools/validate_assets.py` (the JSON manifests
and tables cross-referenced against the shipped data — file existence, name
joins, model and sound references).

Current state: both Win32 configurations built and linked under MSVC as of
the end of Phase 6's first half. Treat the cross-check as a fast first pass,
not a verdict: it is a different compiler, it cannot link, and the section
above lists what that costs. The CI builds remain the authority.

**Phase 8 stages A and B now have a real build.**
[PR #6](https://github.com/Zwaliebaba/Outpost.Warzone/pull/6) builds and
links them under MSVC in both Win32 configurations, so the two compiler
signals agree: the cross-checker is clean at 195 units — down from 198 as
three files were folded away — and CI is green on the same commit.

**They have still not been run.** That remains the signal that matters for a
renderer, and the visual checklist in
[Phase8Plan.md](Phase8Plan.md#verification) is outstanding for both stages.
Stage B especially wants the device-loss path exercised, since collapsing the
state caches is exactly what that stresses.

### The runsheet

Five phases now owe a running build, each with a checklist in its own document.
[Verification.md](Verification.md) gathers them into one ordered session —
eight passes, each screen visited once, with what "pass" means for each item and
a table to record results in. It also carries the two things that are not
checklist items: the working-directory failure that looks exactly like a
rendering fault, and the `visfog` key inverting its own name, which would have
had the fog comparison verifying the wrong fog.

Two of the passes settle open decisions rather than confirming existing work.
Pass C is the precondition for Phase 8 D2 and Phase 6 B4, which both thread a
`D3DPOOL_DEFAULT` resource through the device-loss path. Pass D answers D1b
outright, in either direction, without any of it being written first.

### What Phase 2 needed beyond the build

A green build says nothing about whether a renderer draws. Phase 2 was checked
by running the game: the title screen, and then a campaign level booted
directly with `-window -game CAM_1A`, which puts the 3D world, the HUD, the
terrain, the units and the translucent build overlay on screen in one shot
without needing menu input.

`Neuron::Fatal` calls `__debugbreak()`, so an assertion under a launcher shows
up only as exit code `0xC0000003` with no message. The message goes to
`OutputDebugString`, so a listener attached to the shared `DBWIN_BUFFER`
mapping is what turns that back into a diagnosis — that is how "Couldn't open
`wrf\demo\democam3.gam`" was distinguished from a rendering failure. Worth
knowing for Phases 3 to 6, which face the same asymmetry.

`tools/dbg.py` is that listener. It was an empty placeholder until the runsheet
needed it; it is now a reader for the `DBWIN_BUFFER` protocol, and like the
files under `tools/stubs/` it is a transcription of somebody else's API and says
so at the top. Start it before the game — every pass in
[Verification.md](Verification.md) can fail in a way that is indistinguishable
from a rendering fault until the message is visible.
