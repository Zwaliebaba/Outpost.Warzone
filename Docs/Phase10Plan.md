# Phase 10 — Renderer maths onto DirectXMath

Working plan for replacing the renderer's fixed-point maths — the 4.12
matrix stack, the sine table, and the hand-rolled vector helpers — with
**DirectXMath** used natively: `XMMATRIX`/`XMVECTOR` computation and
`XMFLOAT3`/`XMFLOAT4X4` storage, composed at the call sites with the `XM*`
functions themselves. **No wrapper functions or classes**: the end state has
no `Vec3`, no `Matrix` type of our own, and no maths function whose body is
one `XM*` call. What survives as named functions is renderer *state and
policy* — the world-matrix stack and the world→screen projection — not
arithmetic.

Phase 8 saw this coming and fenced it off: its plan keeps "the fixed-point,
pre-transformed-vertex pipeline" on the grounds that "changing them is not
simplification, it is a second project." This is that second project, scoped
to the maths only. The pipeline architecture — CPU transform,
`D3DFVF_XYZRHW` vertices, `DrawPrimitiveUP`, the software clipper — does
**not** change in this phase; the arithmetic inside it moves from 4.12 fixed
point to float SIMD.

**Status: planned, not started.** The [decisions to confirm](#decisions-to-confirm)
are open. Figures were measured against the tree at the head of this branch;
the method is at the [end](#measurement).

## Why DirectXMath, and what it costs to adopt

`<DirectXMath.h>` ships in the Windows SDK — header-only, `namespace
DirectX`, nothing to link, nothing to restore. Under
[AGENTS.md R14](../AGENTS.md) that makes it part of what the build is already
allowed to depend on, alongside `d3d9.h`. It is the designed successor to
exactly this kind of code: D3DX's maths went deprecated with the June 2010
SDK and DirectXMath is its replacement.

Two platform facts make it a fit rather than a gamble:

- **The convention matches.** DirectXMath is row-vector, row-major — the
  same convention the fixed-point stack already uses (`x' = x·a + y·d + z·g + j`
  is a row vector times a 3×4 row-major matrix). The
  [equivalence](#the-arithmetic-equivalence) below is exact, not
  approximate.
- **x86 is supported with care.** MSVC for x86 defaults to `/arch:SSE2` (the
  v145 toolset included), which is all DirectXMath needs. The known x86
  traps are alignment ones: `XMVECTOR`/`XMMATRIX` want 16-byte alignment,
  which locals get from the compiler but struct members and heap objects do
  not. The discipline is the documented one — `XMFLOAT3`/`XMFLOAT4X4` at
  rest, `XMMATRIX`/`XMVECTOR` in registers, `XMLoad*`/`XMStore*` between
  them, `alignas(16)` on the one global stack array, and `XM_CALLCONV`/
  `FXMMATRIX` parameter conventions on the few functions that take matrices
  by value.

## Where the maths stands

### The module

`NeuronClient/RenderMatrix.h` (128 lines) and `RenderMatrix.cpp` (394 lines)
are the whole of it:

- **`SDMATRIX`** — a 3×4 matrix of `SDWORD`s in 4.12 fixed point
  (`FP12_SHIFT`), in an 8-deep stack (`aMatrixStack`, `psMatrix`,
  `_MATRIX_INDEX`).
- **`aSinTable`** — 5,120 `int` entries: a 4,096-entry sine cycle plus a
  quarter-cycle so `COS` is `SIN` offset by 1,024. Values are 4.12; the
  index is a 16-bit binary angle (65,536 = full circle, `DEG_360`) dropped
  to 12 bits (`>> 4`). Built by `pie_MatInit` at startup.
- **Functions** — `pie_MatBegin`/`pie_MatEnd` (push/pop),
  `pie_MatRotX/Y/Z`, `pie_RotProj` (world→screen with depth returned),
  `pie_VectorNormalise` (an octagonal-approximation normalise),
  `pie_SurfaceNormal`, `pie_SetGeometricOffset`, and the winding tests
  `pie_Clockwise`/`pie_PieClockwise`.
- **Macros** — `SIN`/`COS`, `pie_TRANSLATE`, `pie_MATTRANS`,
  `pie_SETUP_ROTATE_PROJECT`/`pie_ROTATE_PROJECT` (an open-coded
  `pie_RotProj`), `pie_ROTATE_TRANSLATE`.

`RenderModel.cpp`'s vertex loop does not call any of this — it reads
`psMatrix->a..l` directly and open-codes transform and projection per model
vertex (`RenderModel.cpp:199-222`). That loop is the hot path the migration
most wants on SIMD.

### The callers, measured

Occurrences in `.cpp`/`.h`, engine (`NeuronCore`+`NeuronClient`, includes
each symbol's declaration and definition) against game (`Outpost`):

| Symbol | Engine | Game |
|---|---:|---:|
| `pie_MatBegin` / `pie_MatEnd` | 3 / 3 | 45 / 44 |
| `pie_MatRotX` / `Y` / `Z` | 2 / 2 / 2 | 41 / 51 / 18 |
| `pie_TRANSLATE` | 1 | 109 |
| `pie_MATTRANS` | 1 | 3 |
| `pie_RotProj` | 2 | 20 |
| `pie_ROTATE_PROJECT` (+ its `SETUP`) | 1 | 7 (+7) |
| `pie_ROTATE_TRANSLATE` | 1 | 2 |
| `pie_VectorNormalise` | 5 | 2 |
| `pie_SurfaceNormal` | 3 | 8 |
| `pie_SetGeometricOffset` | 2 | 17 |
| `SIN(` / `COS(` | 7 / 7 | 12 / 11 |

Roughly **390 game-side sites across 20 files**, dominated by
`Display3D.cpp` (146 matrix-family occurrences), `Component.cpp` and
`Effects.cpp` (52 each), `Bucket3D.cpp` (24), `MapDisplay.cpp` (19). The
matrix stack is how the game expresses every hierarchical transform — body →
turret → weapon → muzzle — so **the call sites are the migration**; there is
no way to go native underneath them and leave them reading `pie_MatRotY(x)`
without that becoming precisely the wrapper layer this phase forbids.

### Adjacent hand-rolled maths in the renderer

- **`IMDLoad.cpp`** — `pie_SurfaceNormal` per polygon at model load, and a
  bounding-sphere pass (`imd_load_points`) in `double` arithmetic on
  `iVectorf`.
- **`BSPIMD.cpp`** — private `iNormalise`, `iCrossProduct`,
  `GetTriangleNormal` on `iVectorf`.
- **`PIEVECTORF`** — a `float x, y, z` struct, layout-identical to
  `XMFLOAT3`, used for camera state (`WarCAM.h`), weather particles
  (`Atmos.h`) and effects (`Effects.h`), each with hand-rolled
  component-wise arithmetic around it.

### What is *not* renderer maths (proposed out of scope)

**`NeuronCore/Trig.cpp`** (181 lines): `trigSin`/`trigCos`/`trigInvCos`/
`trigIntSqrt` lookup tables, 52 occurrences across 8 files — and the callers
are **simulation**: `Move.cpp` (14), `Projectile.cpp` (7), `Formation.cpp`,
`Action.cpp`, `OptimisePath.cpp`, plus `WarCAM.cpp` (12, camera logic).
Droid movement and projectile flight are gameplay state, not presentation.
It could move to `XMScalarSin`/`sqrtf` mechanically, but that changes
simulation arithmetic for no rendering benefit and belongs to a simulation
phase if anywhere. Decision 2 below.

Likewise the integer *game* geometry — `dirtySqrt`, `calcDirection`,
map/tile arithmetic in `Outpost/Geometry.cpp` and friends — stays. Only its
calls **into** the render maths (`pie_RotProj` etc.) change form.

## The arithmetic equivalence

The reason this migration is mechanical rather than a re-derivation: each
fixed-point operation is exactly a DirectXMath function under the row-vector
convention. Derived from `RenderMatrix.cpp` against the DirectXMath
reference:

| Fixed point | DirectXMath | Note |
|---|---|---|
| `pie_MatRotX(a)` | `world = XMMatrixRotationX(r) * world` | `r = a · 2π/65536`; element-for-element identical rotation block |
| `pie_MatRotY(a)` / `Z(a)` | `XMMatrixRotationY(r)` / `Z(r)`, same shape | same |
| `pie_TRANSLATE(x,y,z)` | `world = XMMatrixTranslation(x,y,z) * world` | pre-multiply = local transform, as today |
| `pie_MATTRANS(x,y,z)` | `world.r[3] = XMVectorSet(x,y,z,1)` | overwrites the translation row |
| `pie_MatBegin`/`End` | push/pop of the stack — state, stays a function pair | |
| transform in `pie_RotProj` | `XMVector3Transform(v, world)` | |
| projection | `sx = xcentre + 1024·vx/vz`, `sy = ycentre − 1024·vy/vz` | `xpshift`/`ypshift` are the constant 10 (`PieMode.cpp:70`), so the focal scale is exactly 1024.0f |
| depth | `vz · 4` | `FP12 >> STRETCHED_Z_SHIFT` = ×4096/1024; keeps `MIN_STRETCHED_Z`, `LONG_WAY`, `MAX_Z` and the Bucket3D sort ranges unchanged |
| `SIN(a)`/`COS(a)` | `XMScalarSinCos(&s, &c, r)` | callers currently `>> FP12_SHIFT` after multiplying; the float form multiplies directly |
| `pie_VectorNormalise` | `XMVector3Normalize` | replaces an octagonal *approximation* — results improve slightly |
| `pie_SurfaceNormal` | `XMVector3Normalize(XMVector3Cross(a, b))` | same operand order as today, so winding/sign is preserved |
| `DEG(x)` then rotate | `XMConvertToRadians(float(x))` | one conversion instead of two, and skips `DEG`'s truncation to integer binary angle |

The behind-camera convention survives untouched: `LONG_WAY` written to both
screen coordinates when `vz` is at or behind the eye, exactly as Phase 8's
D1b analysis requires.

**What changes numerically.** The table quantises angles to 1/4096 of a
circle and values to 1/4096; each fixed-point multiply truncates; the
projection divides in integers; the normalise is an approximation. Float
removes all four errors, so screen positions move by sub-pixel amounts and
lighting normals sharpen slightly. That is a visual-parity question, not a
correctness one, and stage E measures it rather than asserting it.
**Determinism**: every machine in a multiplayer session runs the same x86
binary under `/arch:SSE2`, so float results are identical across peers. The
one simulation-adjacent consumer — the muzzle-position calculation
(`Droid.cpp:4048`, `Structure.cpp:5985`, via `pie_ROTATE_TRANSLATE`) feeding
projectile spawn — stays deterministic for the same reason.

## The target

```
RenderMatrix.h/.cpp  →  the world-matrix stack and projection state, on XMMATRIX:
    an alignas(16) XMMATRIX stack + depth index        (state, was SDMATRIX stack)
    Neuron::MatrixPush() / Neuron::MatrixPop()         (was pie_MatBegin/End)
    Neuron::WorldMatrix()  → XMMATRIX&                 (call sites compose natively)
    Neuron::ProjectToScreen(const XMFLOAT3&, POINT&) → float depth   (was pie_RotProj
                                                        and the ROTATE_PROJECT macros)
    Neuron::SetGeometricOffset(x, y)                   (unchanged role)
    inline constexpr float RadiansPerWorldAngle = XM_2PI / 65536.0f;
```

Deleted outright, no successor: `SDMATRIX`, `aSinTable` and its `pie_MatInit`
build loop, `SIN`/`COS`, `pie_MatRotX/Y/Z`, `pie_TRANSLATE`, `pie_MATTRANS`,
`pie_ROTATE_TRANSLATE`, `pie_VectorNormalise`, `pie_SurfaceNormal`,
`iVectorf`, `PIEVECTORF` (becomes `XMFLOAT3`), and the
[already-dead items](#stage-a) below.

A representative call site, `Display3D.cpp`'s structure turret:

```cpp
// today                                          // target
pie_MatBegin();                                   Neuron::MatrixPush();
pie_TRANSLATE(c->x, c->z, c->y);                  XMMATRIX& w = Neuron::WorldMatrix();
pie_MatRotY(DEG(-(SDWORD)turretRotation));        w = XMMatrixTranslation(float(c->x), float(c->z), float(c->y)) * w;
pie_MatRotX(DEG(turretPitch));                    w = XMMatrixRotationY(XMConvertToRadians(-float(turretRotation))) * w;
...                                               w = XMMatrixRotationX(XMConvertToRadians(float(turretPitch))) * w;
pie_MatEnd();                                     ...
                                                  Neuron::MatrixPop();
```

The arithmetic is DirectXMath's own at the call site — no function of ours
between the game and `XMMatrixRotationY`. `MatrixPush`/`MatrixPop`/
`ProjectToScreen` are the renderer's state and its screen mapping, which is
the part that was never maths.

The model-vertex loop in `RenderModel.cpp` becomes an `XMVector3Transform`
per point (loaded with `XMLoadSInt3` over the `iVector` triple — decision 3)
followed by the same float projection, with the `pie_RAISE`/
`pie_HEIGHT_SCALED` y-adjustments applied before the load as they are today.
If profiling ever asks for more, `XMVector3TransformStream` over a float
point array is the next step — that is decision 3's second option, not this
phase's default.

## Stages

Each stage ends green on Debug and Release, `check_case.py` clean, and — per
Phase 8's rule, since every stage touches rendering — **run**, not just
built, once a Windows environment is available: `Debug\Outpost.exe -window
-game CAM_1A` plus the relevant [Verification.md](Verification.md) passes.

### A — Dead-maths sweep  *(behaviour-preserving)*

Zero callers anywhere in the tree, feature-macro allow-list checked, per the
[AGENTS.md §6](../AGENTS.md) rule:

- `pie_MatCreate` — defined, not even declared in the header.
- `pie_VectorInverseRotate0` — declared and defined, never called.
- `pie_INVTRANSX/Y/Z` — macros, no expansion sites.
- `pie_CLOCKWISE` — macro, no expansion sites (`pie_Clockwise`, the
  function, also has zero callers; `pie_PieClockwise` is live ×2 in
  `RenderModel.cpp` and stays).
- `X_INTERCEPT` — macro private to `RenderMatrix.cpp`, unused.
- `pie_MatReset` — called only by `pie_MatInit`; folds in.

### B — Engine-internal swap, existing signatures kept  *(temporary shims)*

Rebuild `RenderMatrix.cpp` on an `XMMATRIX` stack, and keep the `pie_*`
signatures for one stage as forwarding shims so the tree stays green while
stage C walks the call sites. The shims are explicitly transitional —
decision 4 — and every one of them dies in stage C/D; none survives to the
end state.

- `SDMATRIX` → `alignas(16) XMMATRIX` stack; `pie_MatRotY(a)` becomes the
  one-line pre-multiply above, still taking binary angles at the seam.
- `RenderModel.cpp`'s vertex loop goes native immediately (it reads the
  matrix directly, so it cannot shim): `XMLoadSInt3` +
  `XMVector3Transform` + float projection, `LONG_WAY` semantics identical.
- `IMDLoad.cpp`: `pie_SurfaceNormal` call sites → cross/normalise on
  `XMVECTOR`; the `double` bounding-sphere pass → float `XMVECTOR`
  arithmetic (`iVectorf` loses its last user).
- `BSPIMD.cpp`: `iNormalise`/`iCrossProduct`/`GetTriangleNormal` →
  `XMVector3Normalize`/`XMVector3Cross` at their call sites.
- `aSinTable` and `SIN`/`COS` survive stage B untouched — game code still
  uses them — but move behind a "stage C deletes this" comment.
- **Parity instrumentation**, temporary, `_DEBUG`-only: compute the old
  fixed-point transform alongside the new float one for `pie_RotProj` and
  the model loop, accumulate the maximum screen-space divergence over a
  CAM_1A run, report it through `Neuron::DebugTrace` at shutdown. The
  number goes in this document; the instrumentation comes out in stage D.

### C — Call sites go native, file by file

Rewrite the ~390 game-side sites to compose `XMMATRIX` directly, smallest
files first so the pattern is settled before `Display3D.cpp`:
the one-liners (`MultiInt`, `MultiMenu`, `MultiLimit`, `Design`,
`Transporter`, `Radar`), then `Bridge`, `Projectile`, `Atmos`, `Lighting`,
`IntDisplay`, `MapDisplay`, `Geometry`, `Bucket3D`, `Structure`, `Droid`,
`Effects`, `Component`, and `Display3D.cpp` last. Along the way:

- `SIN`/`COS` sites → `XMScalarSinCos` on radians; the `>> FP12_SHIFT`
  rescales disappear into float multiplies.
- `pie_RotProj` and the `pie_ROTATE_PROJECT` macro sites →
  `Neuron::ProjectToScreen`. The `(iPoint*)&struct.sx` punning at the
  `tileScreenInfo` sites gets replaced with writes to the actual fields.
- `pie_ROTATE_TRANSLATE` (the two muzzle sites) → `XMVector3Transform`
  against `Neuron::WorldMatrix()`.
- `PIEVECTORF` → `DirectX::XMFLOAT3` (layout-identical), and the
  hand-rolled component arithmetic in `WarCAM.cpp`, `Atmos.cpp` and
  `Effects.cpp` onto `XMVECTOR` ops where the expression is vector-shaped
  (decision 6).
- Each `pie_*` shim is deleted in the same commit as its last caller.

### D — Finish and rename

Delete `aSinTable`, `pie_MatInit`'s table build (init collapses to a stack
reset), the parity instrumentation, and every remaining shim. The module's
public names land per [AGENTS.md §1](../AGENTS.md) in `namespace Neuron` —
checked against the platform headers first, per Phase 8's C4 lesson.
`RenderMatrix.h` at that point declares the stack, the projection, the
offset setter and the one conversion constant, and includes
`<DirectXMath.h>`; `Geo.h` (a pure forwarder to `RenderMatrix.h`) is folded
away if nothing else justifies it. `MigrationPlan.md` gets the measured
before/after.

### E — Verification

- `tools/crosscheck.py` both configurations. mingw-w64 ships
  `<directxmath.h>`; if the pinned CI/container version predates it or
  disagrees with MSVC's, the established `tools/stubs/` pattern applies —
  a transcription that checks our usage, saying so at the top.
- MSVC CI, Debug and Release Win32.
- The stage-B parity figure: expected bound is ~1 pixel of screen-space
  divergence; anything larger is a conversion bug to find, not a tolerance
  to widen.
- The run: CAM_1A boot per [Verification.md](Verification.md), with
  attention on the passes this phase touches most — terrain and model
  placement (any systematic offset means a projection-constant error),
  turret/muzzle hierarchies, radar rotation, effects circles
  (`SIN`/`COS` sites), and the sequence-player/UI screens that use
  `pie_SetGeometricOffset`.

## Decisions to confirm

1. **The game-side call sites are in scope.** ~390 sites across 20
   `Outpost/` files change form. Unavoidable given "no wrappers": leaving
   the `pie_*` signatures in place *is* a wrapper layer. **Recommended:
   yes.**
2. **`NeuronCore/Trig.cpp` is out of scope.** Its callers are simulation
   (`Move`, `Projectile`, `Formation`, `Action`) plus `WarCAM`; it is not
   renderer maths. A follow-up could retire it separately.
   **Recommended: out of scope.**
3. **Model point storage stays integer this phase.** `iIMDShape::points`
   remain `iVector` (the model format and loader are game data, per Phase
   8), loaded per-vertex with `XMLoadSInt3`. The alternative — `XMFLOAT3`
   storage converted at load — buys `XMVector3TransformStream` for the hot
   loop but floats data that simulation also reads (`connectors`, extents)
   and touches the loader. **Recommended: integer now; float storage as a
   measured follow-up if the loop shows up in a profile.**
4. **Temporary shims during B–C are acceptable.** Strictly avoiding them
   would force the engine swap and all ~390 call sites into one atomic
   change across 23 files. The shims exist for the life of two stages,
   shrink monotonically, and the end state has none. **Recommended: yes.**
5. **Angle units in game state stay as they are** (binary angles and
   degrees in the object structs), converted to radians at the point a
   rotation matrix is built. Migrating stored angles to float radians would
   reach deep into simulation state for no rendering gain.
   **Recommended: yes.**
6. **`PIEVECTORF` → `XMFLOAT3` is in scope**, including moving the
   vector-shaped arithmetic in `WarCAM`/`Atmos`/`Effects` onto `XMVECTOR`
   ops. It is the renderer's float vector type leaked into presentation
   state, and layout-identical to `XMFLOAT3`. The narrower alternative is a
   pure type swap, leaving the component-wise arithmetic as it is.
   **Recommended: in scope, with the type swap landing first and the
   arithmetic following only where the expression genuinely is a vector
   op.**

## Measurement

Symbol counts: `grep -rIo` over `*.cpp`/`*.h` per project directory, so a
line using a symbol twice counts twice; engine counts include the
declaration and definition. File totals: `grep -c` of the combined
matrix-family pattern per file. Line counts: `wc -l` at the head of this
branch. The projection constants were read from `PieMode.cpp:70-71`
(`xpshift = ypshift = 10`) and `RenderTypes.h` (`FP12_SHIFT`,
`STRETCHED_Z_SHIFT`, `MIN_STRETCHED_Z`, `LONG_WAY`). The rotation
equivalence was derived by expanding `pie_MatRotX/Y/Z`'s element updates
into row operations and comparing against the DirectXMath row-vector
rotation matrices element by element.
