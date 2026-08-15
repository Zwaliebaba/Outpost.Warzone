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

The legacy DirectX surface was **well contained**: roughly 271 COM call sites,
almost all of them in ~15 NeuronCore files, with game code in `Outpost/`
barely touching DirectX directly. That containment is what made the graphics
work tractable, and it held: Phase 2 touched 50 files, of which six are under
`Outpost/` and three of those are a one-line include or symbol rename. What
remains of the 271 call sites is DirectInput, DirectPlay and DirectSound —
Phases 3, 4 and 5.

Subsystems in use today:

- **Graphics** — **done, see Phase 2.** Direct3D 9 throughout: an
  `IDirect3DDevice9` owned by `Screen.cpp` and drawn through by
  `D3DRender.cpp`, `D3DMode.cpp` and `TexMan.cpp`. Was DirectDraw 4 surfaces
  plus a Direct3D 6 immediate-mode device.
- **Input** — DirectInput 7 (`DXInput.cpp`, `DIRECTINPUT_VERSION=0x0700`).
- **Audio** — QSound's QMixer (`QSTrack.cpp`, `Aud.cpp`) over DirectSound, plus
  MCI CD audio (`CDAudio.cpp`) and CD spanning (`CDSpan.cpp`).
- **Video** — `Sequence.cpp` streams `.rpl` movies via `WINSTR.LIB`, now into
  the Direct3D 9 back buffer. The decoder itself is Phase 6.
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
  optimisation and nothing in the design blocks them.
- **The device name settings.** `war_SetDirectDrawDeviceName` and
  `pie_SetDirect3DDeviceName` still store the strings the old code matched
  driver GUIDs against. Nothing selects a device by name now — the framework
  takes the default adapter and falls back by capability — so the `-D3D`,
  `-RGB` and `-REF` switches and the matching config entries no longer choose
  anything. They are left in place because the config file format is not this
  phase's to change.
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

### Verification

`NetTest/` is a console harness CI runs in both configurations: two processes
over 127.0.0.1, one hosting and one joining, checking the certificate and
handshake, 120 reliable messages at nine sizes verified per byte and per
sequence number, datagrams, and that a broadcast never returns to its sender.
It passed first time. **This is the first phase whose result was actually run
rather than only compiled** — which mattered, because whether Schannel accepts
a self-signed certificate generated by `HostCertificate.cpp` is not a question
compiling can answer.

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

Current state: both Win32 configurations build and link under MSVC. Treat the
cross-check as a fast first pass, not a verdict: it is a different compiler,
it cannot link, and the section above lists what that costs. The CI builds
remain the authority.

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
