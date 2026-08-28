# Phase 8 — Native Direct3D 9: retiring the iVis/pie layer

Working plan for collapsing the render abstraction between the game and
Direct3D 9. As with Phases 4 to 6, every figure here was measured against the
tree (at `4633436`) rather than estimated; the method is a tree-wide grep per
symbol, cross-checked against the `.vcxproj` files and the feature-macro
allow-list in `tools/check_case.py`, per [AGENTS.md §6](../AGENTS.md).

**The four gating decisions are settled** — owner decision, 2026-08-15,
recorded with their reasoning under [Decisions](#decisions--settled): the
rename is full, the dead config keys are dropped, the clipper replacement is
attempted behind a parity gate, and stages A and B land now while stage C
waits for Phase 6's `Sequence.cpp` rewrite to merge. **Stages A, B and C are
done**; D is the remainder. Nothing below is
blocked on input; the remaining unknowns are audits (A5) and a measurement
gate (D1), both owned by the stage that runs them.

**Scope statement, because the name is overloaded.** Two things in this
codebase are called "pie". The `.pie`/IMD **model format** — the shape files
under `GameData/`, their loader (`IMDLoad.cpp`) and the `iIMDShape` structures
— is game data and **stays untouched**. *(Still true. The model format has
since been taken up separately, as [NMO](NeuronMeshObject.md) and its
[migration](PieToNmoMigration.md); nothing in this phase changed on account of
it, and nothing in it should.)* What this phase removes is the
`pie_*`/`iV_*` **render layer**: the code that sat between the game and
whichever of five backends (software DDX, Glide, PlayStation, Direct3D 6 RGB,
Direct3D 6 HAL) the machine had. Since Phase 2 there is exactly one backend.
The layer now dispatches to itself, caches state twice, converts vertex
formats per draw, and carries a configuration surface that selects between
renderers that no longer exist.

---

## Where the layer sat at the start of this phase

One UI image was five layers deep:

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

**Stage B did exactly that**, and found the same shape a second time in the
texture-page binding. Both chains are now one call deep to a single cache,
and `g_bStateCacheStale` is gone.

### The blast radius, measured

| | |
|---|---|
| Layer translation units | 14 `.cpp`, 6,185 lines (table below) — **11 `.cpp`, 3,885 lines after Stage B** |
| Layer headers | 17, ~1,670 lines |
| Game TUs referencing `pie_*`/`iV_*` | 51 of 121 in `Outpost/` |
| Engine TUs referencing them | 31 of 85 in `NeuronCore/` (75 units now) |
| Heaviest call sites | `iV_DrawTransImage` 132, `pie_Draw3DShape` 84, `iV_GetImageWidth` 72, `iV_TRANSLATE` 70, `iV_DrawText` 54, `iV_Line` 47, `pie_SetFogStatus` 42 |

### File inventory and verdicts

| File | Lines (start -> now) | What it is | Verdict |
|---|---|---|---|
| `PieMode.cpp` | 211 -> 166 | Init: picks between three "modes" that run identical code; a divide table nothing reads; no-op begin/end wrappers | Collapse into renderer init |
| `D3DMode.cpp` | 221 -> **deleted** (B4) | Nine `_dummyFunc*_D3D` no-ops, empty vsync/palette/`SetTransFilter_D3D`/`TransBoxFill_D3D`, the RGB/HAL/REF trio | Delete; fold `_mode_D3D` into init |
| `RendMode.cpp` | 293 -> 24 | "Video memory" allocator with no callers, `iSurface` create/destroy, the function-pointer dispatch tables | Delete (audit the two `iSurface` users first) |
| `RendFunc.cpp` | 194 -> 39 | Transparency tables built by a function nothing calls; mouse-pointer bookkeeping | Fold the two live functions, delete the rest |
| `PieState.cpp` | 628 -> **deleted** (B1b, into `D3DRender.cpp`) | State cache #1; dead driver-name strings, engine enum, caps, no-op gamma, mouse, swirly-box flags | Merge with `D3DRender.cpp` into one state module |
| `PieDraw.cpp` | 1,179 -> 644 | **Live:** `pie_Draw3DShape`, image quads, line/rect, the poly funnel. **Dead:** a second `#if _MSC_VER` copy of `Draw3DShape`, the BSP draw block, `pie_IvisPoly*`, `pie_DrawTriangle`, `pie_DrawFastTriangle` | Rewrite live half; delete dead half |
| `PieFunc.cpp` | 565 -> 185 | **Live:** viewing window, `pie_TransColouredTriangle`, back-buffer image blit, byte-scale table. **Dead:** `pie_Sky`/`pie_Water`/`pie_Blit`/`pie_CornerBox`/`pie_AddFogandMist`/3dfx query | Split |
| `PieBlitFunc.cpp` | 603 -> 591 | UI image quads, radar, backdrop load — live, minus no-ops and a duplicate | Becomes the 2D module |
| `PieClip.cpp` | 1,071 | Software polygon/line clipping (live), screen-size globals | Keep for now; retirement is stage D |
| `PieMatrix.cpp` | 396 | Fixed-point matrix stack, rotate/project — live, also used by game logic | Keep as is (rename only) |
| `PiePalette.cpp` | 316 | Palette, shade tables, nearest-colour — live, the assets are palettised | Keep |
| `PieTexture.cpp` | 54 -> **deleted** (B3, into `Tex.cpp`) | Two one-line wrappers around `dtm_*` | Fold into `TexMan` |
| `Tex.cpp` | 364 -> 365 | Texture-page name/bookkeeping table mirroring `TexMan`'s pages | Merge into `TexMan` |
| `Ivi.cpp` | 90 -> 88 | Legacy error/abort/shutdown plumbing | Delete; fold shutdown |
| `D3DRender.cpp` | 582 -> 1,005 (absorbed `PieState.cpp` and `D3DMode.cpp`) | The real device path: states, `DrawPrimitiveUP`, reset handling | **Nucleus of the new renderer** |
| `TexMan.cpp` | 351 | The real texture pages (managed pool, `A8R8G8B8`) | Stays |
| `Screen.cpp` | 1,190 | Device, present, back-buffer lock, backdrop | Stays |
| `TextDraw.cpp` | 1,061 -> 1,003 | Fonts as textured quads + FMV subtitle path | Consumer; loses two dead functions |

Headers: `RendMode.h` (138) and `IvisPatch.h` (106) were pure alias tables —
`#define iV_DrawImage pie_ImageFileID` and so on — and the tables went in C1.
`IvisDef.h`, `PieDef.h` and `PieTypes.h` carried the live type definitions
(`iIMDShape`, `PIEVERTEX`, `IMAGEFILE`); C3 consolidated them into
`RenderTypes.h` and `Model.h`, and all three are gone. `RendMode.h` survives
as the surface module and now owns `iSurface`.

---

## What the analysis found

> **Read this section in the past tense.** It describes the tree as it stood
> at `4633436`, before Stage A, and the line numbers and symbol names are
> from that commit. Almost everything it names as dead has since been
> deleted — that was the point — so it is kept as the evidence the stages
> were justified by, not as a description of the code today. For what the
> tree looks like now, see the stage status blocks below.

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
| BSP rendering | `DrawBSPIMD` (`BSPIMD.cpp:367`) has no callers; `DrawTriangleList` + the `BSPimd`/`BSPObject`/`BSPCamera` globals in `PieDraw.cpp` are reached from nothing. **BSP loading is live** — `_imd_load_bsp` parses BSP chunks that exist in shipped `.pie` files — so only the render half goes; the loader is a data-format question this phase does not open. *(Opened later, elsewhere: [PieToNmoMigration.md](PieToNmoMigration.md) §5.8 drops the trees at conversion, since 63 files carry them and nothing has traversed one since stage A.)* |
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

**Where the work stands.** Stages A and B put the shape in place under the
old names, deliberately — stage C does the renaming, so B could stay
mechanical. C2 has since landed the names themselves, so the filenames in
the diagram above are now the filenames on disk: `Render.cpp` holds the one
state block, the vertex funnel, `BeginFrameD3D`/`EndFrameD3D` and the
device-reset recovery, having absorbed `PieState.cpp` and `D3DMode.cpp`;
`RenderModel.cpp` and `Render2D.cpp` are the model/2D pair;
`RenderMatrix.cpp`, `RenderClip.cpp` and `Palette.cpp` are renamed with
their internals untouched, as planned.

Two gaps remain against the diagram. The texture table is single, with
`PieTexture.cpp` absorbed into `Tex.cpp`, but the `Tex.cpp`/`TexMan.cpp`
split into "the page table" and "the device textures" survives one more file
boundary than the diagram wants — closing it is a merge, not more surgery.
And the symbols still carry `pie_`/`iV_` prefixes even where the files no
longer do (`pie_D3DRenderForFlip` now lives in `Render2D.cpp`); that is the
symbol rename still ahead of C3.

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

### A — Remove what is provably dead  *(behaviour-preserving)*

**Status: done**, in five commits, one per sub-stage. The estimate was
"roughly 2,400 lines"; the measured result is **3,024 deletions against 85
insertions across 55 files**, and the fourteen layer translation units drop
from 6,185 lines to 4,573 — 26% of the layer, before any restructuring.
`RendMode.cpp` is the extreme case, 293 lines down to 24. What the stage
turned up that this plan did not predict is recorded
[below](#what-stage-a-turned-up).

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
  `Config.cpp` — drop-and-ignore is decided (Decision 2): verify that
  unknown keys are skipped on read, then stop reading and writing them,
  leaving old config files working; the commented video-options menu and `VIDEO` title mode out
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

#### What Stage A turned up

Seven things this plan did not have right, or did not know:

1. **`renderSky` moved from A2 into A1.** It is the only caller of
   `iV_UniBitmapDepth`, one of the never-assigned function pointers, so it
   could not outlive A1's deletion of that pointer. It had no caller of its
   own, so it could only ever have jumped through null.
2. **`drawTexturedTile` and `drawMapTile2` are dead for different reasons**,
   and only one of them is a no-op. `drawTexturedTile` issues real draws
   (`pie_DrawTile`, `iV_Line`) but is unreachable, because `Display3D.cpp`
   includes `Bucket3D.h` and `BUCKET` is therefore always defined.
   `drawMapTile2` is reachable — from the live intelligence-screen map
   render — but every draw it issues is a no-op. Both go; the second needed
   a side-effect audit to establish, since it sits inside a function whose
   other calls do draw.
3. **The `SetBSP*` setters had live call sites in five files**, which the
   "zero call sites" framing above missed. They write statics nothing reads,
   so the calls went too, taking `CalcBSPCameraPos`, `GetRealCameraPos` and
   `GetCameraDistance` with them.
4. **`d3dFog` was nested inside the dead `renderMode` branch** of
   `loadRenderMode`. It is live configuration and had to be lifted out
   rather than deleted with its enclosing block — the one place in A3 where
   dead and live were genuinely interleaved.
5. **The gamma feature is inert but user-facing.** `pie_SetGammaValue`'s
   body was `pieStateCount++`, so the slider and its two key bindings have
   done nothing since Phase 2 dropped the DirectDraw gamma ramp. A4 removed
   the no-op and its six call sites but deliberately kept the value, its
   registry key and the keys that change it: removing a user-facing binding
   is a decision about the feature, not dead-code removal. **Left open:**
   implement it against `SetGammaRamp`, or drop it by owner decision.
6. **`iV_GetMouseFrame` had to be kept.** The rest of the cursor cluster is
   dead, but this one backs the `EXTID_CURSOR` script variable, and script
   bindings are compiled into shipped `.slo` files where an unknown name is
   a load failure — the trap Phase 6 documented for `playCDAudio`. It has
   returned zero for as long as `iV_SetMousePointer` has had no callers, and
   still does.
7. **`releaseMapSurface` freed with `delete[]` memory that
   `iV_SurfaceCreate` had obtained from `malloc`.** A latent defect, retired
   by deleting both.

Two things were left alone on purpose. The commented-out **graphics**
options menu shares a comment block with the video options menu A3 deleted,
but it configures fog and translucency, which are live settings, so it is
not this phase's to remove. And `loadRenderMode` contains the **`resolution`
block twice, verbatim** — pre-existing, idempotent, and nothing to do with
renderer selection; noted rather than fixed as a drive-by.

One incidental behaviour change: `-D3D`, `-RGB` and `-REF` each forced
640x480 as a side effect of selecting a renderer, and no longer do. Unknown
tokens fall through to `else {}`, so an old shortcut still launches;
resolution remains separately configured.

**Verification.** Every sub-stage ends with `tools/crosscheck.py` clean in
both Debug and Release (198/198 units, the same count as the pre-change
baseline) and `tools/check_case.py` clean. **It builds and links under MSVC**
— CI on [PR #6](https://github.com/Zwaliebaba/Outpost.Warzone/pull/6) is
green for Debug and Release Win32 on the final commit of Stage B, which
carries Stage A with it. **It has not been run**: there is no Windows
toolchain in this container, and per [AGENTS.md §3](../AGENTS.md) that makes
the visual checklist below outstanding for the whole of Stage A, not merely
advisable.

### B — Collapse the funnels  *(behaviour-preserving by construction)*

**Status: done**, in five commits. B1 was split in two — B1a for the cache
collapse, which is the semantic change, and B1b for the file merge, which is
mechanical — so that a regression bisects to one or the other. The stage
removed **933 lines against 584 insertions across 22 files**, and deleted
three translation units (`PieState.cpp`, `PieTexture.cpp`, `D3DMode.cpp`),
taking `NeuronCore` from 78 project entries to 75. The layer is now **11
files, 3,885 lines**, against 14 files and 6,185 at the start of Stage A —
37% gone. **CI on [PR #6](https://github.com/Zwaliebaba/Outpost.Warzone/pull/6)
builds and links both stages under MSVC in Debug and Release Win32**, so the
cross-checker and the real compiler agree; neither stage has been run.
What the stage turned up is under
[What Stage B turned up](#what-stage-b-turned-up).

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

#### What Stage B turned up

**The double cache was a pattern, not an instance.** The plan named the
translucency state; the texture page binding had exactly the same shape, and
its second copy was worse. `pie_SetTexturePage` cached the page and
`dtm_SetTexturePage` cached it again, but only the second is restored by
`dtm_RestoreTextures`, and `pie_ResetStates` never touched `texPage` at all
— so the pie-side copy went stale after every device reset and was harmless
only because the real binding was put back underneath it. Both caches are
now single, in the module that owns the device call.

**Three structs turned out to be entirely write-only**, each found by
following a cache rather than by grep: `PIED3DPOLY`'s `flags` (B2),
`D3DINFO`'s `bAlphaKey` — the last field standing after A3 removed
`D3DGetAlphaKey`, set to `FALSE` by one function and `TRUE` by the next
(B4) — and `rendStates.texPage` (B3).

**Two more computed-and-never-read tables**, on top of the ones Stage A
found: `_iVPRIM_DIVTABLE`, 1024 reciprocals rebuilt on every init, and
`iSurface::scantable`, 4KB per surface. `scantable` looked live —
`IntelMap.cpp` passed `scantable[1]` to `seq_BlitBufferToScreen` as a stride
— but that function ignores both the buffer and the stride it is handed and
uses its own video buffer. The call now passes `rendSurface.width`, which is
the value `scantable[1]` held, so it stays correct even if the callee is
later fixed to honour it.

**One latent defect preserved rather than fixed.** `pie_DrawPoly` computed
the same off-screen test as every other draw path and stored the result in
`PIED3DPOLY::flags`, which nothing read — so a terrain polygon with an
off-screen vertex has always been drawn rather than culled. Removing the
struct made this visible. Stage B keeps the behaviour and documents it at
the site: making it cull changes what appears on screen, which belongs to
the visual pass, not to deleting a struct.

**Two bounds that disagreed.** `iV_TEX_MAX` allowed 48 texture pages while
the device only ever created 32, so a page past 32 was written into the name
table and then refused by the texture manager, leaving a filled-in entry and
returning -1. One array now, one bound.

**Deliberately not done:** the plan's B2 item about the 2D quad fillers
writing `D3DTLVERTEX` directly. The `PIEVERTEX` conversion is also where the
off-screen bailout and the half-texel offset live, so bypassing it means
duplicating both — the opposite of what this stage is for. It becomes free
once the funnel takes a vertex-buffer, which is D2.

### C — Rename to the target shape, delete the alias layer

The full rename is decided (Decision 1). Phase 6 has merged, so the wait
described in [Sequencing](#sequencing-against-phases-6-and-7) is over and
this stage is **done**: C1, C2, C3 and the symbol rename have all landed.

- **C1. Kill the alias tables.** *(done)* Deleted `RendMode.h`'s
  `#define iV_* pie_*` block, `TextDraw.h`'s pair and `IvisPatch.h`'s 34,
  landing the canonical names at 630 call sites.
- **C2. File moves.** *(done)* `git mv` to the target layout (table above),
  with `#include` lines, project/`.filters` entries and include guards
  following in the same commit. Files that are renamed-but-not-rewritten
  (`RenderMatrix`, `RenderClip`, `Palette`) keep their internals — renaming
  their every local is churn Phase 7 owns. Symbol renames were deliberately
  kept out: mixing them into a file move makes the diff unreadable.
- **C3. Header consolidation.** *(done)* The four headers reduced to the two
  the target called for, plus a home for the declarations they were carrying:

  | was | is |
  |---|---|
  | `PieTypes.h` + `PieDef.h`'s types and constants | `RenderTypes.h` (280 lines) |
  | `IvisDef.h`'s `iIMDShape` family | `Model.h` (119 lines) |
  | `IvisDef.h`'s `IMAGEFILE` family | `BitImage.h`, beside its functions |
  | `IvisDef.h`'s `iSurface` | `RendMode.h`, beside `rendSurface` |
  | `PieDef.h`'s eight `pie_Draw*` declarations | `RenderModel.h` (new, 30 lines) |
  | `Ivi.h`'s `DIVSHIFT` | `RenderClip.cpp`, its only user |
  | `Ivi.cpp`'s two lifecycle functions | `PieMode`, beside `pie_Initialise` |

  `Ivi.h`, `Ivi.cpp`, `IvisDef.h` and `PieDef.h` are gone; `PieTypes.h`
  became `RenderTypes.h`. `PIEPOLY` went to `RenderModel.cpp`, its only
  user and only via `static` functions, so it never needed to be public.
- **C4. The symbol rename.** *(done)* The `iV_` prefix became
  `namespace Neuron` rather than being stripped — see below.
- **C3a. The error and debug shims.** *(done)* `iV_Error`'s 47 sites became
  `Neuron::DebugTrace`; `iV_Stop`, `iV_Abort` and the whole inert
  `iV_DEBUG0..12` family were deleted with `Bug.h`/`Bug.cpp`.

**Risk: low but wide.** Mechanical; the danger is a missed alias in a
comment or a `.filters` mismatch, both of which CI and `check_case.py`
catch.

#### What Stage C turned up

- **A word-boundary rename can eat its own definition.** `Ivi.h` carried a
  duplicate `#define iV_POLY_MAX_POINTS pie_MAX_POLY_SIZE` — an alias whose
  target was itself an alias. Rewriting alias names blind turned it into
  `#define pie_MAX_POLY_SIZE pie_MAX_POLY_SIZE`, which is self-referential,
  so the preprocessor stops expanding it: the real `#define
  pie_MAX_POLY_SIZE 16` in `PieDef.h` was shadowed and
  `static iVertex xclip[pie_MAX_POLY_SIZE + 4]` lost its array size. The
  duplicate was deleted; a sweep for `^#define\s+(\w+)\s+\1` found no other
  instance. Any future mass-rename wants that grep as a post-step.
- **The `iV_` prefix was a namespace in disguise.** Checking all 87 rename
  targets against the mingw-w64 Win32 and CRT headers before touching
  anything found 8 platform collisions. Two were severe: `iV_HeapAlloc` and
  `iV_HeapFree` are macros, so stripping them to `HeapAlloc`/`HeapFree`
  would have hijacked every kernel32 call in every translation unit;
  `CreateFontIndirect`, `GetCharWidth` and `SetFont` are GDI. The functions
  went into `namespace Neuron` instead. **Run that check before the `pie_`
  rename.**
- **A macro that expands to nothing never type-checks its arguments.**
  `iV_DEBUG` is defined in no configuration, so all 60 `iV_DEBUG0..12` sites
  had been inert for years — long enough for `Tex.cpp` to be logging
  `buffer`, a local of a *different function*. A tree-wide sweep found this
  was the only always-empty parameterised macro with call sites, so the
  class is closed.
- **Two wrappers would have recursed into themselves.** `IntOrder.cpp` had
  `static GetImageWidth`/`GetImageHeight` forwarding to the `iV_` functions
  of the same name. Dropping the prefix makes each call itself — which
  compiles clean and blows the stack. Neither the cross-checker nor CI
  would have caught it. They were `UWORD`→`UDWORD`→`UWORD` no-ops and were
  deleted.
- **Most headers were never self-contained.** Splitting `IvisDef.h` and
  `PieDef.h` showed that ~30 headers compiled only because a hub header
  happened to arrive first through somebody else's include list —
  `Render2D.h` takes `IMAGEFILE` parameters and included nothing at all.
  Deriving each home header's public surface from the header itself, then
  checking every file's transitive include closure against it, found them
  in one pass instead of one 10-minute cross-check at a time. That script
  is the tool to reach for next time a hub header is split.
- **Debug CI does not cover the linker.** Removing
  `ImageHasSafeExceptionHandlers=false` (during B6, on the assumption it
  went with `WINSTR.LIB`) passed Debug and failed Release with LNK2026 /
  LNK1281. Incremental linking silently disables `/SAFESEH`, so only the
  `/INCREMENTAL:NO` Release config exercises it. The property is back, with
  the real reason recorded at the site: `dinput8.lib` in `DX9\Lib` predates
  SafeSEH and its `dilib1.obj` carries no handler table. Re-enabling
  SafeSEH needs a clean `dinput8.lib` first — logged, not scheduled.

### D — Follow-up simplifications

**Status: D3 done, D1 split and half done, D2 open.** The two items that could
be settled without a running build were, and the analysis that D1's parity gate
needs is below.

- **D1. Software clip → device clip.** Sanctioned (Decision 3), gated on
  parity: point the funnel at the viewport and a scissor rect
  (`pie_Set2DClip` becomes `SetScissorRect`), keep the behind-camera
  `LONG_WAY` convention, and delete `RenderClip.cpp` **only
  if** side-by-side screenshots at screen edges and of the radar
  viewing window are acceptable — the interpolation difference (affine vs
  perspective-correct at clip boundaries) is real, if likely invisible at
  these depths. If parity fails, the clipper stays and this item closes as
  attempted-and-rejected, with the screenshots kept as the record.
  - **D1a. The unreachable half.** *(done)* 449 lines deleted, ungated —
    see below.
  - **D1b. The replacement itself.** *(open)* Still gated on the screenshots,
    and now with a precondition D1 did not know it had.
- **D2. Dynamic vertex buffer + quad batching.** *(open — deferral
  recommended, see below.)* The Phase 2 follow-up; the
  single funnel from B2 is the precondition. UI-heavy screens issue
  hundreds of 4-vertex `DrawPrimitiveUP` calls per frame.
- **D3. Texel-offset switch.** *(done)* Resolved to **always on**, which is
  what every configuration already ran: `Config.cpp` defaulted the key to 1
  and wrote it back, so only a hand-edited registry ever saw it off. The
  offset is a constant, and `D3DSetTexelOffsetState`, the `TexelOffsetOn` key
  and the write-only `g_bTexelOffsetOn` flag are gone. The key is dropped
  rather than reset, on A3's precedent.

#### What stage D turned up

- **43% of the clipper was unreachable, and the sweep that should have found
  it could not.** `RenderClip.cpp` held two clipping families — one over
  `PIEVERTEX`, which the renderer uses, and one over `iVertex`, which nothing
  calls. `pie_PolyClipTex2D` and its two edge helpers, plus `_xclip_edge2d`
  and `_yclip_edge2d`, are 449 lines with no call site anywhere; and
  `pie_PolyClip2D` was declared in the header without ever being defined.
  Stage A's tree-wide grep missed them because two are `extern` in
  `RenderClip.h`: the declaration and the definition are two hits, which reads
  as used. **A grep proves a symbol is dead only if it distinguishes
  declarations from calls** — the sharper test here was that `iVertex` now
  appears nowhere in the file. The clipper is 595 lines, so D1b's prize is
  that, not the ~1,000 this plan costed it at.
- **The behind-camera cull does survive D1b, and the funnel must *not* be
  widened.** An earlier revision of this section said the opposite — that
  `D3D_PIEPolygon` testing only `sy > LONG_TEST` where the clipper tests `sx`
  as well would drop polygons far off to the side, and that the funnel's cull
  had to widen first. Following it through the projection shows that is wrong,
  and acting on it would have caused the regression it was trying to prevent.

  The sentinel is written to **both** coordinates together — the two places
  that mark a vertex as behind the camera set `d3dx` *and* `d3dy` to
  `LONG_WAY` in the same breath
  ([RenderModel.cpp:210](../NeuronClient/RenderModel.cpp#L210) and
  [:215](../NeuronClient/RenderModel.cpp#L215)) — so a test on `sy` alone
  catches every one of them. `pie_ClipTextured`'s own guard is an exact
  equality, `sx == LONG_WAY || sy == -LONG_WAY`, whose second half tests a
  value nothing ever writes.

  Widening the funnel's cull to `sx > LONG_TEST || sy > LONG_TEST` would then
  be a regression, because a large `sx` is not only a sentinel: a vertex close
  to the camera and far to the side projects there legitimately. Seven live
  draw sites already pass such vertices to the funnel unclipped (below), and
  the device rasterises them correctly today. A wider cull would discard the
  whole primitive instead.

  What *is* load-bearing is narrower and sits on one path:
  `pie_ClipTexturedTriangleFast`'s `sx > LONG_TEST` test protects
  `pie_TransColouredTriangle`, whose caller
  ([Display3D.cpp:4915](../Outpost/Display3D.cpp#L4915)) admits a triangle when
  only **one** of its three vertices is on screen, leaving the other two
  anywhere at all. That single test is what D1b has to account for — not the
  funnel.
- **D1's premise is already running in production, which makes the parity test
  cheap.** `bClip` is a parameter, not a constant
  ([RenderModel.cpp:515](../NeuronClient/RenderModel.cpp#L515)), and seven live
  sites in `Render2D.cpp` pass `FALSE`: the box outlines and the filled rects
  hand raw screen-space vertices straight to `DrawPrimitiveUP` and let the
  device clip them at the viewport. That is exactly what D1 proposes to do
  everywhere, so the question is not whether it works but whether it looks the
  same.

  It also means the gate can be run **before** any rewrite: force `bClip` to
  `FALSE` on the clipped paths, screenshot, compare. If the difference is
  invisible there, the `SetScissorRect` work is worth starting; if it is not,
  D1b closes as attempted-and-rejected without a line of it being written. The
  scissor rect is still needed for the sub-rects `pie_Set2DClip` sets — the
  radar and the design screen — since a viewport alone does not express those.
- **The parity gate is not asking what it looks like it is asking.**
  `pie_ClipXT`/`pie_ClipYT` interpolate position, `tu`, `tv`, `sz`, colour and
  specular linearly in screen space, in fixed point (`>> DIVSHIFT`), and the
  funnel then derives `rhw` as `1/sz` from that affine `sz`. So the vertices
  the clipper emits at a boundary are *already* an affine approximation, and a
  scissor rect — clipping at rasterisation, perspective-correct throughout —
  would be the more correct of the two. The question the screenshots answer is
  not "is the device version worse" but "does the difference show at all".
  That reframes the gate; it does not remove it, because a visible change is
  a visible change whichever direction it is.
- **D2's cost is not where the plan looked.** A dynamic vertex buffer must
  live in `D3DPOOL_DEFAULT` — `D3DUSAGE_DYNAMIC` forbids `MANAGED` — so it has
  to be released before `Reset` and recreated after, which puts a new resource
  into the device-loss path. That path is the one stage B disturbed most, by
  collapsing the two state caches it depends on, and
  [it has never been run](#verification). The win on the other side is small:
  the game issues hundreds of draw calls per frame at 640x480, which no
  hardware this will run on notices. **Recommendation: D2 after the visual
  checklist has been run once**, so device loss is known good before something
  new is threaded through it. Quad batching proper is a further step again —
  merging draws requires consecutive ones to share state, and getting that
  wrong reorders translucency.

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
`Sequence.cpp` makes**, so C must not interleave with the backend rewrite.

**The ordering is decided (Decision 4): A and B land now; C starts after
Phase 6's `Sequence.cpp` rewrite merges.** Phase 6's backend therefore lands
into a smaller tree but an unchanged vocabulary — it keeps writing
`pie_ScreenFlip`/`pie_RenderImageToBackBuffer` calls and stage C renames
them with everything else. Stage D1 additionally follows Phase 6's B4 if
both happen, since both re-plumb the same back-buffer paths.

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

## Decisions — settled

All four questions were put to the owner and answered on 2026-08-15; each
answer took the recommendation. Recorded here with the reasoning, so the
stages above can cite them rather than reargue them.

1. **Scope of the rename (C1): full.** Every `pie_*`/`iV_*` call site is
   renamed to the new API and the alias headers are deleted. The owner has
   sanctioned wide transformation; half-renaming would leave two
   vocabularies alive indefinitely, which is exactly the `iV_`/`pie_`
   situation this phase exists to end. The rejected alternatives — keeping
   `pie_*` as the permanent public API, or renaming only the rewritten core
   behind thin wrappers — both trade ~700 mechanical edits now for a
   permanent split vocabulary, and the edits are scripted and reviewable
   one module per commit.
2. **Config-file compatibility (A3): drop and ignore.** The dead keys
   (`rendMode`, both device names) stop being read and written; existing
   config files keep working because unknown keys are skipped on read —
   which A3 verifies in `Config.cpp` before deleting anything. Rejected:
   writing dead keys forever (permanent dead weight, no upside) and a
   versioned config reset (discards users' live settings to remove entries
   that are already ignored).
3. **Software clipper (D1): attempt replacement, gated on parity.** Device
   viewport/scissor clipping is tried; `RenderClip.cpp` is deleted only if
   side-by-side screenshots at screen edges and of the radar viewing window
   pass. If parity fails, the clipper stays — it is correct and
   battle-tested, and nothing forces the trade. The gate and its record are
   specified in D1.
4. **Phase ordering: A and B now, C after Phase 6.** The dead-code removal
   and funnel collapse land immediately — they avoid Phase 6's contact
   surface entirely — and the call-site rename waits for the `Sequence.cpp`
   rewrite to merge so the two never interleave in the same files.
   Rejected: running all of Phase 8 first (would force Phase 6's plan and
   in-flight work to track renamed symbols) and finishing Phase 6 first
   (parks 2,400 lines of provably dead code behind an unrelated
   asset-conversion project).

**Not a decision, an audit:** the `iSurface` residue (A5) — the button and
map surface allocations feeding nothing in D3D mode. The plan's course is
delete-after-per-user-audit; the audit itself is the gate, and it stays
inside stage A where the evidence is gathered.
