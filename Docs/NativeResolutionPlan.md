# Native world resolution

**By owner decision: the 3D world is to be drawn at the desktop's own
resolution, and the interface is to stay on the scaled logical canvas it lays
out on today.**

This is a follow-up to [the display change](MigrationPlan.md) of 2026-08-16,
not a replacement for it. That change made the *window* the desktop; this one
makes the *world* the desktop. They are different claims, and the gap between
them is what this document is about.

Status key: **Done** — in the tree. **Stage** — planned work, in the order
given. **Out of scope** — deliberately not part of this, with the reason.

---

## Where the tree is today

`frameInitialise` ([Window.cpp](../NeuronClient/Window.cpp)) makes the process
DPI-aware, reads the desktop metrics, and derives two sizes:

- **Physical** — `screenWidth`/`screenHeight` in
  [Screen.cpp](../NeuronClient/Screen.cpp). The window, the back buffer and
  the D3D viewport. This really is the desktop.
- **Logical** — the `pie_GetVideoBufferWidth/Height` canvas in
  [RenderClip.cpp](../NeuronClient/RenderClip.cpp), which is the desktop
  divided by an integer `Neuron::DisplayScale`. **Every coordinate the game
  computes is in this space**, the world included.

`ChooseDisplayScale` takes the largest whole factor that leaves the canvas at
least 960x540, so the 640x480-anchored UI keeps roughly one physical size
across densities:

| Desktop | Scale | Logical canvas |
|---|---|---|
| 1920x1080 | 2 | 960x540 |
| 2560x1440 | 2 | 1280x720 |
| 3440x1440 | 2 | 1720x720 |
| 3840x2160 | 4 | 960x540 |

`D3DDrawPoly` ([Render.cpp](../NeuronClient/Render.cpp)) is the single funnel
that multiplies every pre-transformed vertex by the scale on the way to the
device. Input mirrors it: `Input.cpp` divides the physical mouse position by
the scale, so the game, the widgets and picking share one space.

**None of that is wrong, and none of it is being undone.** The UI wants the
logical canvas. The world does not.

---

## The defect: the world is drawn on the interface's grid

Two things follow from the world sharing the UI's canvas, and the second is a
real visual defect rather than a design preference.

**The world is sampled at canvas resolution, not display resolution.** The
projection's focal length is fixed at `1 << xpshift` = 1024 *logical* pixels
([PieMode.cpp](../NeuronClient/PieMode.cpp) sets `xpshift = ypshift = 10`) and
the centre of projection is `rendSurface.width >> 1`, also logical. A 4K
desktop therefore projects the world onto a 960x540 grid and multiplies the
result by four.

**Terrain vertices are quantised to that grid, because the terrain projects
through an integer entry point.** `Neuron::ProjectToScreen`
([RenderMatrix.cpp](../NeuronClient/RenderMatrix.cpp)) returns `SDWORD`
screen coordinates, and `Display3D.cpp` drives the whole 33x33 landscape mesh
through it (`tileScreenInfo[i][j].sx`/`.sy`, three sites — land, water and the
water surface). Every terrain vertex therefore lands on a whole *logical*
pixel, which is a 4-physical-pixel grid at 4K and 2 at 1080p. As the camera
moves, each vertex snaps between those cells rather than sliding: terrain
edges and the texture seams pinned to them crawl.

The model path does **not** have this problem —
[RenderModel.cpp](../NeuronClient/RenderModel.cpp) computes `d3dx`/`d3dy` as
floats from the same matrix and never rounds. So today the units are smooth
and the ground under them is not, which is the tell.

There are roughly 28 `ProjectToScreen` call sites — the terrain mesh,
`Bucket3D.cpp`'s depth sort, HUD anchors in `Display3D.cpp`, `Geometry.cpp`
and `Component.cpp`. They do not all want the same fix, and separating them is
most of the work below.

---

## The target: two spaces, drawn honestly

Keep one logical canvas for the interface. Give the world a projection that
lands directly on the back buffer.

- **The field of view does not change.** The focal length scales with the
  canvas so that the *angle* is identical: `focal = 1024 * scale` against a
  centre of `screenWidth / 2`. Only the sampling rate goes up. This is what
  keeps `VISIBLE_XTILES` out of the change — a fixed 32-tile grid is only a
  hazard if the world view widens, and it does not.
- **The interface is untouched.** Widget layout, `D_W`/`D_H` offsets, the
  radar, the console, tooltips and every `pie_` 2D call keep computing in
  logical coordinates and keep being multiplied by the scale.
- **The funnel learns which space it is being given.** `D3DDrawPoly` scales
  logical submissions and passes physical ones straight through.

---

## Stage 1 — a vertex space on the render state

Add `Neuron::VertexSpace { Logical, Physical }` and a setter beside the
existing render state, defaulting to `Logical`. `D3DDrawPoly` multiplies by
`DisplayScale()` only in `Logical`. Nothing else changes yet, so this stage is
behaviour-identical by construction: no caller sets `Physical`.

Do not add a second cache of this state (AGENTS.md R12) — it lives with the
code that makes the device call, as the scale does today.

**Verifies as:** a normal boot is pixel-identical to the previous build.

## Stage 2 — a float projection

`ProjectToScreen` keeps its `SDWORD` signature and its callers. Add
`Neuron::ProjectToScreenF` returning `float` coordinates through the same
matrix, with the same near-limit behaviour (`LONG_WAY` at
`MIN_STRETCHED_Z`) so the two agree about what is off screen.

Widen `SVMESH`'s `sx`/`sy`/`wx`/`wy` ([Display3D.h](../Outpost/Display3D.h)) to
`float` and move the three terrain sites onto `ProjectToScreenF`. This alone
removes the crawl, still in logical space.

**Verifies as:** terrain edges stop snapping as the camera pans. Compare a
slow pan against the current build.

## Stage 3 — project the world at physical resolution

Give the world pass its own projection parameters: centre at
`screenWidth / 2`, `screenHeight / 2`-derived `geoOffset`, focal length
`1024 * scale`. Set `VertexSpace::Physical` for the duration of the world
pass in `Display3D.cpp` and restore `Logical` before the HUD.

`rendSurface.clip` must follow: the world clips against the physical rectangle
while it is in `Physical`, the UI against the logical one.

Then classify all ~28 `ProjectToScreen` sites. Each is one of three things,
and each needs a decision recorded in the diff:

1. **World geometry** — terrain, model anchors, `Bucket3D` depth. Physical,
   float where precision matters.
2. **A world position anchoring a UI element** — the HUD arrows and target
   boxes in `Display3D.cpp`. These project a world point and then draw a
   *logical* quad at it, so they need the physical result divided by the scale
   at exactly one place. Getting this wrong puts the HUD in the corner at 4K,
   so it is the stage's main review risk.
3. **Picking** — `Geometry.cpp`. The mouse arrives logical
   (`Input.cpp` already divides); it must be multiplied back up to compare
   against a physical projection. One conversion, one place.

`pie_GetResScalingFactor` (`logicalWidth * 100 / 640`) feeds effect radii and
shifts that are now in physical units — it becomes `physicalWidth * 100 / 640`
for those callers. Check all six sites; they are all in `Display3D.cpp`.

**Verifies as:** the world is visibly sharper at 4K and the HUD has not moved.
This is the stage that cannot be reviewed into correctness — it has to be run.

## Stage 4 — record it

Update the display section of [MigrationPlan.md](MigrationPlan.md), which
currently describes one canvas, and add the world-resolution passes to
[Verification.md](Verification.md).

---

## Out of scope, and why

- **Widening the field of view for widescreen.** A real improvement and a
  different change: it needs `VISIBLE_XTILES` to stop being a fixed 32, which
  is a simulation-visible constant (`Atmos.cpp` sizes its particle pool off it,
  `WarCAM.cpp` centres the camera with it). Preserving the FOV is what keeps
  this change to the renderer.
- **Changing resolution at runtime.** The canvas, `DisplayBuffer`, the widget
  root and the radar are all sized once at init. `WM_SIZE` is ignored and there
  is no `WM_DISPLAYCHANGE` handler, so a desktop that changes mode under the
  game keeps the old back buffer. Worth fixing; not this.
- **Re-laying-out the 640x480 interface.** The scale exists precisely so this
  is not required.
- **The CPU compositing paths.** `drawBackDrop` writes
  `640 * scale x 480 * scale` pixels through a locked back buffer **every
  frame** a backdrop is up — 4.9M scalar pixel writes per frame at 4K, on the
  menus and the load screens. `screen_Upload` and the FMV blit are the same
  shape. Moving them to textured quads would delete all three loops, and it is
  its own change.
- **The debug 2D paths** (`screenTextOut` and friends in `Screen.cpp`) are
  reachable only from the `DISP2D` editor tree and are still unscaled. Noted
  in the 2026-08-16 record; still true.

---

## What none of this can tell you

Every stage above is reviewable, and none of it is verifiable without a
Windows run — the tree has never been run at 4K, and the terrain crawl this
document is built on is read out of the code rather than measured off a
screen. [Verification.md](Verification.md) is the runsheet. Stage 2 is the
cheapest place to confirm the diagnosis: if terrain edges do not visibly stop
crawling, the reading above is wrong and stage 3 should not be started.
