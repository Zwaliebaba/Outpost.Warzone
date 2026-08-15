# Phase 8 — Native Direct3D 9: retiring the iVis/pie layer

Working plan for collapsing the render abstraction between the game and
Direct3D 9. As with Phases 4 to 6, every figure here was measured against the
tree (at `4633436`) rather than estimated; the method is a tree-wide grep per
symbol, cross-checked against the `.vcxproj` files and the feature-macro
allow-list in `tools/check_case.py`, per [AGENTS.md §6](../AGENTS.md).

**Scope statement, because the name is overloaded.** Two things in this
codebase are called "pie". The `.pie`/IMD **model format** — the shape files
under `GameData/`, their loader (`IMDLoad.cpp`) and the `iIMDShape` structures
— is game data and **stays untouched**. What this phase removes is the
`pie_*`/`iV_*` **render layer**: the code that sat between the game and
whichever of five backends (software DDX, Glide, PlayStation, Direct3D 6 RGB,
Direct3D 6 HAL) the machine had. Since Phase 2 there is exactly one backend.
The layer now dispatches to itself, caches state twice, converts vertex
formats per draw, and carries a configuration surface that selects between
renderers that no longer exist.

---

## Where the layer sits today

One UI image, today, is five layers deep:

```
iV_DrawTransImage                      macro alias        (RendMode.h)
  → pie_ImageFileID                    state + quad setup (PieBlitFunc.cpp)
    → pie_DrawImage                    PIEVERTEX fill     (PieDraw.cpp)
      → D3D_PIEPolygon                 SDWORD→float, 1/w  (D3DRender.cpp)
        → D3DDrawPoly                  DrawPrimitiveUP    (D3DRender.cpp)
```

and one render state change is three:

```
pie_SetRendMode(REND_ALPHA_TEX)        REND_MODE → 4 combine modes (PieState.cpp)
  → pie_SetTranslucencyMode            cache #1, engine check      (PieState.cpp)
    → D3DSetTranslucencyMode           cache #2, SetRenderState    (D3DRender.cpp)
```

The two caches are the direct cause of the `g_bStateCacheStale` machinery that
Phase 2 had to add for device reset: after a `Reset` the pie-side cache keeps
answering "already set" while the device sits at defaults, so `D3DReInit`
deliberately invalidates one cache and `pie_ResetStates` walks the other
through forced double-toggles (`rendStates.fog = !rendStates.fog` and
friends). One cache, owned by the code that talks to the device, removes the
whole class.

### The blast radius, measured

| | |
|---|---|
| Layer translation units | 14 `.cpp`, 6,185 lines (table below) |
| Layer headers | 17, ~1,670 lines |
| Game TUs referencing `pie_*`/`iV_*` | 51 of 121 in `Outpost/` |
| Engine TUs referencing them | 31 of 85 in `NeuronCore/` |
| Heaviest call sites | `iV_DrawTransImage` 132, `pie_Draw3DShape` 84, `iV_GetImageWidth` 72, `iV_TRANSLATE` 70, `iV_DrawText` 54, `iV_Line` 47, `pie_SetFogStatus` 42 |

### File inventory and verdicts

| File | Lines | What it is | Verdict |
|---|---|---|---|
| `PieMode.cpp` | 211 | Init: picks between three "modes" that run identical code; a divide table nothing reads; no-op begin/end wrappers | Collapse into renderer init |
| `D3DMode.cpp` | 221 | Nine `_dummyFunc*_D3D` no-ops, empty vsync/palette/`SetTransFilter_D3D`/`TransBoxFill_D3D`, the RGB/HAL/REF trio | Delete; fold `_mode_D3D` into init |
| `RendMode.cpp` | 293 | "Video memory" allocator with no callers, `iSurface` create/destroy, the function-pointer dispatch tables | Delete (audit the two `iSurface` users first) |
| `RendFunc.cpp` | 194 | Transparency tables built by a function nothing calls; mouse-pointer bookkeeping | Fold the two live functions, delete the rest |
| `PieState.cpp` | 628 | State cache #1; dead driver-name strings, engine enum, caps, no-op gamma, mouse, swirly-box flags | Merge with `D3DRender.cpp` into one state module |
| `PieDraw.cpp` | 1,179 | **Live:** `pie_Draw3DShape`, image quads, line/rect, the poly funnel. **Dead:** a second `#if _MSC_VER` copy of `Draw3DShape`, the BSP draw block, `pie_IvisPoly*`, `pie_DrawTriangle`, `pie_DrawFastTriangle` | Rewrite live half; delete dead half |
| `PieFunc.cpp` | 565 | **Live:** viewing window, `pie_TransColouredTriangle`, back-buffer image blit, byte-scale table. **Dead:** `pie_Sky`/`pie_Water`/`pie_Blit`/`pie_CornerBox`/`pie_AddFogandMist`/3dfx query | Split |
| `PieBlitFunc.cpp` | 603 | UI image quads, radar, backdrop load — live, minus no-ops and a duplicate | Becomes the 2D module |
| `PieClip.cpp` | 1,071 | Software polygon/line clipping (live), screen-size globals | Keep for now; retirement is stage D |
| `PieMatrix.cpp` | 396 | Fixed-point matrix stack, rotate/project — live, also used by game logic | Keep as is (rename only) |
| `PiePalette.cpp` | 316 | Palette, shade tables, nearest-colour — live, the assets are palettised | Keep |
| `PieTexture.cpp` | 54 | Two one-line wrappers around `dtm_*` | Fold into `TexMan` |
| `Tex.cpp` | 364 | Texture-page name/bookkeeping table mirroring `TexMan`'s pages | Merge into `TexMan` |
| `Ivi.cpp` | 90 | Legacy error/abort/shutdown plumbing | Delete; fold shutdown |
| `D3DRender.cpp` | 582 | The real device path: states, `DrawPrimitiveUP`, reset handling | **Nucleus of the new renderer** |
| `TexMan.cpp` | 351 | The real texture pages (managed pool, `A8R8G8B8`) | Stays |
| `Screen.cpp` | 1,190 | Device, present, back-buffer lock, backdrop | Stays |
| `TextDraw.cpp` | 1,061 | Fonts as textured quads + FMV subtitle path | Consumer; loses two dead functions |

Headers: `RendMode.h` (138) and `IvisPatch.h` (106) are pure alias tables —
`#define iV_DrawImage pie_ImageFileID` and so on — and go entirely.
`IvisDef.h`, `PieDef.h`, `PieTypes.h` carry the live type definitions
(`iIMDShape`, `PIEVERTEX`, `IMAGEFILE`) and survive, consolidated.

---

## What the analysis found

### Every dispatch slot is a no-op or null

The function-pointer tables through which iVis once selected a backend now
contain, in the only mode that exists:

- `iV_pLine`, `iV_pBox`, `iV_pBoxFill`, `iV_ppBitmap`, `iV_ppBitmapTrans`,
  `iV_ppBitmapColourTrans` → `_dummyFunc*_D3D`, which discard their
  arguments. No live caller remains.
- `iV_VSync` → `_vsync_D3D`, empty.
- `iV_SetTransFilter` → `SetTransFilter_D3D`, empty — and this one **is**
  called, four times at `Display.cpp:375-378`, so the four transparency
  tables it is meant to build stay zero-filled. Harmless today because
  everything that reads them is itself dead (below), but it is a loaded gun.
- `iV_TransTriangle`, `iV_tgTriangle`, `iV_tgPolygon`, `iV_UniBitmapDepth`,
  `iV_SetTransImds`, `iV_ScreenDumpToDisk` → **never assigned at all**; a
  call is a jump through null. `renderSky()` (`Display3D.cpp:3931`) calls
  `iV_UniBitmapDepth` twice and would crash — it survives only because
  nothing calls `renderSky`.

### Dead code confirmed by tree-wide grep

Each of these was grepped across both projects, including comments,
`.vcxproj`/`.filters`, and the feature-macro allow-list:

| Symbol(s) | Evidence |
|---|---|
| `pie_Sky`, `pie_Water`, `pie_Blit` | zero call sites |
| `pie_DrawFastTriangle`, `pie_DrawBoundingDisc`, `pie_RectFilter`, `pie_Num3dfxBuffersPending`, `pie_ImageDefTrans` | zero call sites |
| `pie_IvisPoly`/`pie_IvisPolyFrame` | clip then return — the software rasteriser behind them was deleted decades of commits ago |
| `pie_DrawTriangle` | draws nothing (clips, returns). Callers: `drawTexturedTile` — inside `#ifndef BUCKET`, and `BUCKET` is defined in `Bucket3D.h`, so not compiled — and `MapDisplay.cpp:438,493`, which therefore render nothing if reached |
| second `pie_Draw3DShape` | the `#if (_MSC_VER != 1000) && (_MSC_VER != 1020)` / `#else` split selects between two near-identical copies by *Developer Studio 4 vs 5*; the `#else` copy can never compile on v145 |
| BSP rendering | `DrawBSPIMD` (`BSPIMD.cpp:367`) has no callers; `DrawTriangleList` + the `BSPimd`/`BSPObject`/`BSPCamera` globals in `PieDraw.cpp` are reached from nothing. **BSP loading is live** — `_imd_load_bsp` parses BSP chunks that exist in shipped `.pie` files — so only the render half goes; the loader is a data-format question this phase does not open |
| `pie_AddFogandMist` | entire body behind `#if SPECULAR_FOG_AND_MIST` which is `0`, and zero callers anyway |
| `pie_CornerBox` | empty body; only caller is `pie_doWeirdBoxFX`, whose only call site is commented out (`Display3D.cpp:2797`) |
| `iV_VideoMemoryLock`/`Alloc`/`Free` | no callers (`Unlock` only from `iV_ShutDown`); would allocate a screen-sized buffer nothing reads |
| `pie_RenderBlueTintedBitmap`/`DeepBlue` (`TextDraw.cpp`) | write through `psRendSurface->buffer`, which is null in D3D mode; zero callers |
| `pie_GetDitherStatus`/`SetDitherStatus`, `pie_SwirlyBoxes`, `pie_WaveBlit`, `pie_GetMouseID` | setters are called from the front end, the getters never — write-only state |
| `pie_SetGammaValue` | called six times (the gamma slider), body is `pieStateCount++` — the slider has done nothing since Phase 2 dropped DirectDraw gamma ramps |
| `pie_Clear`, `pie_LocalRenderBegin`/`End`, `pie_RenderSetup`, `pie_DrawMouse`, `pie_DownloadDisplayBuffer`, `pie_ScaleBitmapRGB` | empty bodies with live call sites — calls to delete with them |
| `D3DGetAlphaKey`, `D3DINFO.bHardware`/`bReference`/`bZBufferOn` | the struct the three "modes" differ by; nothing reads any field any more |
| `iV_PIEDraw`, `renderSky`, `iV_RenderAssign` in `MapDisplay.cpp` | every call site is inside a comment block |

Total in the delete column, counting the halves of split files: roughly
**2,400 lines**, before any restructuring.

### The renderer-selection configuration selects nothing

This is the "PIE configuration" half of the phase, and it spans both projects:

- **`WAR_REND_MODE`** (`WarzoneConfig.h`): `REND_MODE_RGB/HAL/HAL2/REF`.
  `Init.cpp:704` switches on it — and every arm calls `pie_Initialise`, whose
  own three arms all run `_mode_D3D()`. The enum distinguishes nothing.
- **Command line** (`ClParse.cpp:70-92`): `-D3D`, `-RGB`, `-REF` set the mode
  and a device-name string. Phase 2 left them "because the config file format
  is not this phase's to change"; it is this phase's.
- **Config file** (`Config.cpp:383-408, 508`): reads/writes `rendMode` and
  the DirectDraw/Direct3D device names.
- **Device names, twice**: `war_Set/GetDirectDrawDeviceName` and
  `war_Set/GetDirect3DDeviceName` in `WarzoneConfig.cpp`, mirrored by
  `pie_Set/GetDirectDrawDeviceName` and `pie_Set/GetDirect3DDeviceName`
  storing into 256-byte buffers in the render state struct. Nothing matches
  driver GUIDs against them any more.
- **The video options menu** (`FrontEnd.cpp:964-1059`): offers *Software /
  DirectX / OpenGL / Glide* — referencing `REND_MODE_SOFTWARE` and
  `REND_MODE_GLIDE`, which no longer exist in the enum. It compiles only
  because the whole menu is inside a comment block, as is its `case VIDEO:`
  dispatch and the `VIDEO` title-mode enumerator.
- **`REND_D3D_RGB/HAL/REF` mode numbers** (`RendMode.h`), plus
  `REND_GLIDE_3DFX`, `REND_16BIT`, `REND_PSX`, `iV_MODE_4101`,
  `iV_MODE_SURFACE` — consumed by range checks like
  `rendSurface.usr >= REND_D3D_RGB && rendSurface.usr <= REND_D3D_REF`
  (`Tex.cpp:94`, `IMDLoad.cpp:1243`) that are always true.
- **Engine checks**: `pie_GetRenderEngine() == ENGINE_D3D` in `WinMain.cpp`
  (twice per frame), `pie_Hardware()` in `Lighting.cpp`, `MapDisplay.cpp`,
  `TextDraw.cpp` — all tautologies since Phase 2.

All of it reduces to: the game initialises the one renderer there is.
`pie_GetResScalingFactor` and the `pie_SetVideoBufferWidth/Height` resolution
plumbing are *not* part of this — resolution is real configuration and stays.

### What is genuinely load-bearing (and stays, reshaped)

- **`pie_Draw3DShape`** — not a pass-through. It owns flag policy
  (translucent/additive/raise/height-scaled/button), the fixed-point
  rotate-and-project of every model vertex, texture-animation frame offsets,
  and per-poly backface culling. It *becomes* the model renderer rather than
  sitting in front of one.
- **The fixed-point matrix stack** (`PieMatrix.cpp`) — used by the renderer
  and directly by game logic (`pie_RotProj`, 20 call sites, for screen-space
  hit tests and overlays). Its arithmetic must not change in this phase.
- **The software clipper** (`PieClip.cpp`) — live on every 3D draw path via
  `pie_ClipTextured`. Candidate for replacement by the viewport/scissor
  clipping D3D9 performs on RHW vertices, but that changes interpolation
  (the clipper lerps UV/colour in screen space, affinely; the device does it
  perspective-correct) and interacts with the `LONG_WAY`/`LONG_TEST`
  behind-camera convention. Stage D, behind a visual comparison.
- **State semantics** — `REND_MODE` → blend/alpha-op mapping, depth modes,
  colour-key-as-alpha-test, bilinear policy (off for constant-alpha to avoid
  black edges). The *semantics* survive; the double bookkeeping does not.
- **The palette module** — the art is 8-bit palettised; `pal_*` feeds texture
  upload, UI colours and the 565 conversions.
- **The 2D quad path** — `pie_DrawImage` and the `IMAGEFILE` machinery are
  how every HUD element and glyph draws. Collapses by two layers but keeps
  its interface shape.
- **Back-buffer compositing** — backdrop, FMV, subtitles
  (`pie_RenderImageToBackBuffer`, `pie_UploadDisplayBuffer`,
  `screenLockBackBuffer`). Phase 6 rewrites the FMV half; see sequencing.

---

## The target

```
Outpost/  (game code, ~700 call sites renamed mechanically)
   │
   ├─ RenderModel.cpp   Draw3dShape, terrain polys/tiles, viewing window
   ├─ Render2D.cpp      image quads, text quads, lines, rects, radar, backdrop
   │        │
   │        ▼
   ├─ Render.cpp        one state block, one vertex funnel, BeginFrame/EndFrame,
   │        │           device-reset recovery            (from D3DRender+PieState)
   │        ▼
   ├─ TexMan.cpp        texture pages, one table         (absorbs Tex, PieTexture)
   ├─ RenderMatrix.cpp  fixed-point transform stack      (PieMatrix, renamed)
   ├─ RenderClip.cpp    software clip                    (PieClip, kept — stage D)
   ├─ Palette.cpp       palettes and shade tables        (PiePalette, renamed)
   └─ Screen.cpp        IDirect3DDevice9, Present, back-buffer lock (unchanged)
```

Deliberately **kept** even though a from-scratch D3D9 renderer would not have
them, because changing them is not simplification, it is a second project:

- The fixed-point, pre-transformed-vertex pipeline (`D3DFVF_XYZRHW`). Moving
  transform onto the device is a rewrite of every draw path and of game code
  that consumes screen-space results.
- `DrawPrimitiveUP`. Dynamic vertex buffers stay the follow-up Phase 2 logged.
- The palettised asset pipeline and the 16-bit backdrop/FMV compositing.
- 640×480-relative UI layout and `pie_GetResScalingFactor`.

New and rewritten code follows [AGENTS.md §1](../AGENTS.md); the `pie_`/`iV_`
prefixes disappear at the call sites as each module's rename lands (the owner
has sanctioned wide mechanical transformation for this phase). Every file
add/remove/rename updates both `.vcxproj` and `.filters` in the same commit.

---

## Stages

Each stage ends green on Debug and Release, `check_case.py` clean, and — for
every stage, since all of them touch rendering — **run**, not just built:
`Debug\Outpost.exe -window -game CAM_1A`, plus the checklist under
Verification. Stages are ordered so that each is independently shippable and
the diffs stay reviewable.

### A — Remove what is provably dead  *(behaviour-preserving, ~2,400 lines)*

- **A1. Dispatch tables and dummies.** Delete the function-pointer tables
  from `RendMode.cpp`/`RendFunc.cpp`, all `_dummyFunc*_D3D`, `_vsync_D3D`,
  `_palette_D3D`, `SetTransFilter_D3D`, `TransBoxFill_D3D`, and the four
  no-op `iV_SetTransFilter` call sites in `Display.cpp`. Delete the
  transparency-table builder (`SetTransFilter`, `pie_BuildTransTable`,
  `aTransTable*`) and the two tinted-bitmap writers that read them.
- **A2. Dead draw paths.** The dead half of `PieDraw.cpp` (second
  `Draw3DShape`, `pie_IvisPoly*`, `pie_DrawTriangle` + its unreachable
  callers `drawTexturedTile` and the `MapDisplay` pair, `pie_DrawFastTriangle`,
  the BSP draw block), `pie_Sky`/`pie_Water`/`pie_Blit`/`pie_CornerBox`/
  `pie_doWeirdBoxFX`/`pie_AddFogandMist`/`pie_Num3dfxBuffersPending`,
  `renderSky`, `DrawBSPIMD`/`TraverseTreeAndRender`, `iV_PIEDraw`'s dangling
  declaration, and the video-memory allocator.
- **A3. Renderer-selection configuration.** `WAR_REND_MODE` and both
  device-name pairs out of `WarzoneConfig.*`; the `-D3D`/`-RGB`/`-REF`
  switches out of `ClParse.cpp`; `rendMode` and device names out of
  `Config.cpp` (unknown keys are ignored on read — verify, then stop
  writing them); the commented video-options menu and `VIDEO` title mode out
  of `FrontEnd.*`; `pie_Initialise(mode)` becomes `pie_Initialise()`;
  `REND_D3D_*`/`REND_GLIDE_*`/`REND_PSX`/`iV_MODE_*` constants and the range
  checks that consume them; `ENGINE_D3D`/`pie_GetRenderEngine`/
  `pie_Hardware` and every tautological branch on them (`WinMain.cpp`,
  `Lighting.cpp`, `MapDisplay.cpp`, `TextDraw.cpp`); the write-only state
  pairs (dither, swirly, wave, mouse-ID) and the no-op gamma path with its
  slider wiring.
- **A4. Empty-call cleanup.** Delete `pie_Clear`, `pie_LocalRenderBegin/End`
  (and the `iV_RenderBegin/End` aliases), `pie_RenderSetup`, `pie_DrawMouse`,
  `pie_DownloadDisplayBuffer`, `pie_ScaleBitmapRGB` — call sites included.
- **A5. `iSurface` audit.** `IntDisplay.cpp` allocates ~40 button-render
  surfaces plus buffers (`ObjectSurfaces`, `System0Surfaces`,
  `TopicSurfaces`); `MapDisplay.cpp`/`IntelMap.cpp` hold `pMapSurface`/
  `pIntelMapSurface` and call an empty `renderMapSurface`. In D3D mode
  buttons and the intelligence screen render direct to the back buffer
  (`pie_BUTTON` path), so the expectation is that only the *dimensions* of
  these structs are still read for layout. Prove it per user, then remove the
  buffers and `iV_SurfaceCreate`/`Destroy`, or shrink to a plain
  width/height struct if layout reads remain.

**Risk: low.** Everything here is unreachable, write-only, or a no-op; the
one behavioural question (A5) gets its own audit. Mouse-pointer plumbing
(`pie_SetMouse` is called ten times; nothing reads the stored ID back)
belongs in this audit too — establish how the pointer actually draws before
deleting the bookkeeping.

### B — Collapse the funnels  *(behaviour-preserving by construction)*

- **B1. One state module.** Merge `PieState.cpp` into `D3DRender.cpp`:
  a single state struct owned next to the device calls; the
  `COLOUR_MODE`/`TEX_MODE`/`ALPHA_MODE` triple — whose only observable
  effect is unbinding the texture page for flat modes — folds into the
  `REND_MODE` switch; `pie_SetTranslucencyMode`'s static-locals cache and
  `g_bStateCacheStale` go, because there is now exactly one cache to
  invalidate on reset. Public semantics preserved: `pie_SetRendMode`,
  `pie_SetDepthBufferStatus`, `pie_SetTexturePage`, `pie_SetBilinear`,
  `pie_SetColourKeyedBlack`, fog status/colour, translucent/additive caps.
- **B2. One vertex funnel.** `D3D_PIEPolygon` + `pie_D3DPoly` +
  `D3DDrawPoly` become one entry point; `PIED3DPOLY` (a struct whose flags
  half the fillers zero and nothing downstream reads beyond the cull marker)
  goes; the 2D quad fillers write the final `D3DTLVERTEX` directly instead
  of `PIEVERTEX`-then-convert. The `LONG_TEST` off-screen bailout and the
  colour-key bilinear workaround move inside the funnel, once.
- **B3. One texture table.** `Tex.cpp`'s `_TEX_PAGE[]`/`_TEX_INDEX` name
  table merges with `TexMan.cpp`'s page array; `PieTexture.cpp`'s two
  wrappers dissolve; the `iV_TEX*` accessor macros become functions on the
  merged table. `pie_SetTexturePage`'s cache moves here (it already
  forwards to `dtm_SetTexturePage`).
- **B4. Init/shutdown in one place.** `pie_Initialise` + `_mode_D3D` +
  `rend_InitD3D` + `iV_ShutDown`/`pie_ShutDown`/`_close_D3D` collapse to
  one init and one shutdown with the ordering written down: Screen device →
  renderer states → texture manager → palette. `rendSurface` shrinks to the
  width/height/centre/clip the live code reads (the 4 KB `scantable` per
  surface is software-renderer residue).

**Risk: moderate.** No call-site behaviour changes, but this is the stage
where regressions would be introduced by transcription error. Mitigation:
one module per commit, screenshot comparison after each, and the device-loss
path (alt-tab in fullscreen) exercised explicitly, since state-cache
handling is exactly what it stresses.

### C — Rename to the target shape, delete the alias layer

- **C1. Kill the alias tables.** Delete `RendMode.h`'s `#define iV_* pie_*`
  block and `IvisPatch.h`; scripted, reviewed find-and-replace lands the
  canonical names at every call site, one game module per commit.
- **C2. File moves.** `git mv` to the target layout (table above), both
  project files and `.filters` updated in the same commits. Rewritten files
  get §1 naming (`Neuron` namespace, PascalCase, `m_`/`_param`); files that
  are renamed-but-not-rewritten (`RenderMatrix`, `RenderClip`, `Palette`)
  keep their internals — renaming their every local is churn Phase 7 owns.
- **C3. Header consolidation.** `PieDef.h`/`PieTypes.h`/`IvisDef.h`/`Ivi.h`
  reduce to a render-types header (vertex/state/image types) and a model
  header (`iIMDShape` family). `Ivi.cpp`'s `iV_Error`/`iV_Stop`/`iV_Abort`
  give way to `Debug.h` calls at their ~30 sites.

**Risk: low but wide.** Mechanical; the danger is a missed alias in a
comment or a `.filters` mismatch, both of which CI and `check_case.py`
catch.

### D — Optional simplifications  *(each needs its own decision + parity check)*

- **D1. Software clip → device clip.** Point the funnel at the viewport and
  a scissor rect (`pie_Set2DClip` becomes `SetScissorRect`), keep the
  behind-camera `LONG_WAY` convention, delete `RenderClip.cpp` (~1,000
  lines). Needs side-by-side screenshots at screen edges and of the radar
  viewing window; the interpolation difference (affine vs
  perspective-correct at clip boundaries) is real, if likely invisible at
  these depths.
- **D2. Dynamic vertex buffer + quad batching.** The Phase 2 follow-up; the
  single funnel from B2 is the precondition. UI-heavy screens issue
  hundreds of 4-vertex `DrawPrimitiveUP` calls per frame.
- **D3. Texel-offset switch.** `D3DSetTexelOffsetState` is a per-card
  Direct3D 6 workaround kept "so the setting still does something"; decide
  whether the half-texel offset is wanted always or never, and delete the
  switch.

---

## Sequencing against Phases 6 and 7

Phase 6's remaining work rewrites `Sequence.cpp`/`SeqDisp.cpp` onto Media
Foundation and a dynamic texture, and its B4 stage retires the lockable back
buffer. The contact surface with this phase is small but real:
`pie_ScreenFlip`'s `CLEAR_OFF` contract, `pie_RenderImageToBackBuffer`, the
subtitle path in `TextDraw.cpp`, and `screenLockBackBuffer` itself.

**Stages A and B do not touch that surface** (A1's deletions inside
`TextDraw.cpp` are to functions the subtitle path never calls) and can land
before, after, or between Phase 6's stages. **Stage C renames the calls
`Sequence.cpp` makes**, so C should land either before Phase 6's B3 backend
rewrite starts or after it merges — not interleaved. Stage D1 must follow
Phase 6's B4 if both happen, since both re-plumb the same back-buffer paths.

Phase 7 (incremental modernisation) picks up whatever this phase renames but
does not rewrite — `RenderClip`'s internals, `Palette`'s globals, the
`UDWORD`-family typedefs in the surviving headers.

---

## Verification

- Per commit: Debug + Release Win32 build, `python tools/check_case.py`,
  `tools/crosscheck.py` as the fast first pass. CI stays the authority.
- Per stage: `Debug\Outpost.exe -window -game CAM_1A`, plus a front-end
  visit (backdrop, menus, options sliders) and a design-screen visit
  (3D component buttons), against reference screenshots taken before A1.
- The checklist, assembled from every path this phase touches:
  terrain incl. water translucency; unit models incl. team-colour texture
  animation frames; additive weapon/explosion effects; the translucent build
  overlay; HUD images plain, tiled and stretched (`iV_DrawImageRect`,
  `iV_DrawStretchImage`); text in both fonts, coloured text, rotated text
  (`pie_DrawImage270`); console and `pie_TransBoxFill` filter boxes; radar,
  rotated radar, and the radar viewing-window quad; the intelligence screen;
  backdrop screens and the loading-screen keep-frame (`CLEAR_OFF`); an FMV
  with subtitles; device loss and recovery via alt-tab in fullscreen, twice.
- Draw-call counters (`pie_GetResetCounts`) before/after stage B on the same
  scene: state-change count should drop (one cache, no forced re-sends),
  poly count should be identical — a cheap regression tripwire.

## Decisions

1. **Scope of the rename (C1): recommended full.** The owner has sanctioned
   transformation; half-renaming leaves two vocabularies alive indefinitely,
   which is the current situation with `iV_`/`pie_`. The alternative — keep
   `pie_*` as the permanent public API and only collapse beneath it — saves
   ~700 mechanical edits and is the fallback if diff size becomes a problem.
2. **Config-file compatibility (A3): recommended drop-and-ignore.** Unknown
   keys are skipped on read (verify in `Config.cpp` first), so old installs
   keep working; the alternative of writing dead keys forever has no upside.
3. **Software clipper (D1): recommended attempt, gated on parity
   screenshots.** It is the single biggest remaining file, but it is also
   correct and battle-tested; nothing forces the trade.
4. **`iSurface` residue (A5): recommended delete after per-user audit** —
   the allocations are real memory (dozens of KB) feeding nothing.
5. **Phase ordering: recommended A and B now, C after Phase 6's
   `Sequence.cpp` rewrite merges.** A and B shrink the tree Phase 6's
   backend lands into without touching its contact surface.
