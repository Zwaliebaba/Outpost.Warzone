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

**Status: planned, not started; the six gating decisions are settled by
owner decision (2026-08-16)** — the record is under
[Decisions](#decisions--settled). Two answers went beyond the plan's
recommendation and widened the scope: `NeuronCore/Trig.cpp` is **in** this
phase, and the angle units stored in game state migrate to **float
radians** — both are stage E. Figures were measured against the tree at the
head of this branch; the method is at the [end](#measurement).

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

### The simulation trig — in scope by decision 2

**`NeuronCore/Trig.cpp`** (181 lines): `trigSin`/`trigCos`/`trigInvSin`/
`trigInvCos`/`trigIntSqrt` lookup tables, 52 occurrences across 8 files.
The callers are **simulation**: `Move.cpp` (14), `Projectile.cpp` (7),
`Formation.cpp`, `Action.cpp`, `OptimisePath.cpp`, plus `WarCAM.cpp` (12,
camera logic). The plan proposed leaving it out as gameplay rather than
renderer maths; the owner ruled it **in** — decision 2. It is retired in
stage E together with the angle-unit migration, because the two touch the
same files and the degree-based `trigSin(angle)` API only collapses cleanly
once the angle it is handed is already in radians. The tables quantise to
whole degrees and 1/4096 steps; `XMScalarSin`/`XMScalarACos`/`sqrtf` are
exact, so movement and projectile arithmetic change numerically at the
sub-degree level — same-binary determinism holds, as below.

### The angle units in game state — migrating by decision 5

Stored angles today come in three unit systems, and decision 5 migrates all
of them to float radians:

- **Integer degrees** on every game object: `SIMPLE_ELEMENTS` in `Base.h`
  carries `UWORD direction`, `SWORD pitch`, `SWORD roll`, and
  `DROID`/`STRUCTURE` add `UWORD turretRotation`/`turretPitch`. Measured
  field references: 104 `->direction`, 49 `->pitch`, 19 `->roll`, 46
  `->turretRotation`, 42 `->turretPitch`, plus 16 dotted — **~276 sites**.
  `MOVE_CONTROL::dir`/`bumpDir` (`SWORD`, degrees) and the `RayCast.h`
  pitch helpers ride along.
- **16-bit binary angles** (65,536 = circle) in the camera:
  `player.r` (`iView`), the `imdRot`/`imdRot2` locals in `Display3D.cpp`,
  and `WarCAM.cpp`'s float `trackingCamera.rotation` family, which stores
  binary-angle *values* in floats and wraps by `DEG(360)`.
- **The `DEG(x)` bridge** between the two — ~150 call sites (measured
  per-file: `Display3D` 39, `WarCAM` 34, `Component` 20, `Effects` 16, …)
  — all of which cease to exist when both sides speak radians.

Three boundaries keep their integer-degree representation, converted at the
edge: the **v≤8 `.bjo` level readers** (`Game.cpp`'s `DROIDINIT`/
`SAVE_STRUCTURE`/`SAVE_FEATURE` carry `UDWORD direction` on disk, and the
shipped levels must stay readable), the **net wire** (`MultiSync.cpp` packs
`direction` as an integer via `NetAdd2` and reconciles with `dir % 360`;
the wire format stays, the pack/unpack converts), and nothing else — the
script VM was checked and exposes no angle field.

Still out of scope: the integer *game* geometry — `dirtySqrt`,
`calcDirection`, map/tile arithmetic in `Outpost/Geometry.cpp` and friends.
Only its calls **into** the render maths (`pie_RotProj` etc.) change form.

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
correctness one, and the stage-B instrumentation measures it rather than
asserting it.
**Determinism**: every machine in a multiplayer session runs the same x86
binary under `/arch:SSE2`, so float results are identical across peers. The
one simulation-adjacent consumer — the muzzle-position calculation
(`Droid.cpp:4048`, `Structure.cpp:5985`, via `pie_ROTATE_TRANSLATE`) feeding
projectile spawn — stays deterministic for the same reason. Stage E extends
the same argument to movement and combat: the arithmetic changes numerically
(the trig tables' whole-degree quantisation goes), but changes identically
on every peer.

## The target

```
RenderMatrix.h/.cpp  →  the world-matrix stack and projection state, on XMMATRIX:
    an alignas(16) XMMATRIX stack + depth index        (state, was SDMATRIX stack)
    Neuron::MatrixPush() / Neuron::MatrixPop()         (was pie_MatBegin/End)
    Neuron::WorldMatrix()  → XMMATRIX&                 (call sites compose natively)
    Neuron::ProjectToScreen(const XMFLOAT3&, POINT&) → float depth   (was pie_RotProj
                                                        and the ROTATE_PROJECT macros)
    Neuron::SetGeometricOffset(x, y)                   (unchanged role)
    inline constexpr float RadiansPerWorldAngle = XM_2PI / 65536.0f;   (transitional —
                                                        dies in stage E with the last
                                                        binary-angle state)
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
the part that was never maths. The example shows the stage-C form; stage E
then collapses `XMConvertToRadians(-float(turretRotation))` to
`-psStructure->turretRotation` when the field itself becomes radians.

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

**Status: done.** 88 lines deleted, no insertions beyond the two-line
inline of `pie_MatReset`'s body: `RenderMatrix.cpp` 394 → 316,
`RenderMatrix.h` 128 → 118. Evidence was a whole-tree grep per symbol over
every file type (so `.vcxproj`/`.filters` and feature-macro-gated code are
covered), each returning only the symbol's own declaration/definition;
after the sweep the same grep returns nothing. `tools/check_case.py` is
clean. mingw-w64 is not installed in the development container, so the
cross-check did not run; MSVC CI is the compile authority for the stage.
The items, each with zero callers anywhere in the tree, feature-macro
allow-list checked, per the [AGENTS.md §6](../AGENTS.md) rule:

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

### E — Angle state to radians, and Trig.cpp retired

The stage that decisions 2 and 5 added. It runs **after** D so the renderer
work is complete and verifiable on its own before simulation state moves;
its first commit is the **unit audit** — a table in this document of every
angle-typed field, its unit and its wrap convention, confirmed against the
code rather than assumed. Then, in order:

- **The fields change type**: `direction`/`pitch`/`roll` in
  `SIMPLE_ELEMENTS`, `turretRotation`/`turretPitch` in `DROID` and
  `STRUCTURE`, `MOVE_CONTROL::dir`/`bumpDir`, the `RayCast.h` helpers —
  `float`, radians. `player.r` becomes `XMFLOAT3` radians (splitting
  `iView` into integer position and float rotation), and `WarCAM.cpp`'s
  rotation/velocity/acceleration triples — already float, but holding
  binary-angle values — rescale to radians.
- **The wrap arithmetic is rewritten, not transliterated.** `% 360`,
  `+= DEG(360)`, `> 180` comparisons and the `(a - b + 360) % 360`
  shortest-turn idiom in `Move.cpp`, `WarCAM.cpp` and `MultiSync.cpp`
  become `XMScalarModAngle` (which wraps to (−π, π]) and explicit
  range logic. This is the stage's real risk: each site is a semantic
  rewrite with a sign/range convention to get right, which is why the
  audit commit comes first.
- **The boundaries convert**: the v≤8 `.bjo` level readers turn on-disk
  integer degrees into radians at load; `MultiSync.cpp` keeps the wire
  format and converts at pack/unpack; effects/audio consumers of
  `direction` follow the field.
- **`Trig.cpp` goes.** With the state in radians, `trigSin(a)` is
  `XMScalarSin(a)` (the degree conversion that made the table API sticky
  no longer exists), `trigInvSin`/`trigInvCos` are
  `XMScalarASin`/`XMScalarACos` returning radians the caller now wants,
  `trigIntSqrt` is `sqrtf`, and the `trigInitialise`/`trigShutDown` pair
  and their `Window.cpp` call sites (lines 377/470) are deleted with the
  tables. `Trig.h`'s `DEG_TO_RAD`/`RAD_TO_DEG` macros and `RenderTypes.h`'s
  `DEG`/`DEG_1`/`DEG_2`/`DEG_60` go once their last user does;
  `RadiansPerWorldAngle` from stage B dies here too.

The stage is game-wide but shallow per site; it lands file-by-file like
stage C, movement (`Move.cpp`) and sync (`MultiSync.cpp`) last with a
skirmish run between them.

### F — Verification

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
- After stage E, gameplay joins the checklist: droid movement and turning,
  formation ordering, projectile arcs and hits, turret tracking, and the
  tracking camera — plus a two-instance multiplayer session long enough to
  show the direction-sync tolerance checks holding in radians.

## Decisions — settled

All six were put to the owner and settled on 2026-08-16. Two rulings widen
the plan beyond its recommendation; the plan text above reflects the
rulings, not the original proposals.

1. **The game-side call sites are in scope — confirmed as recommended.**
   ~390 sites across 20 `Outpost/` files change form. Unavoidable given
   "no wrappers": leaving the `pie_*` signatures in place *is* a wrapper
   layer.
2. **`NeuronCore/Trig.cpp` is IN this phase — ruled beyond the
   recommendation.** The plan proposed it out of scope as simulation
   maths; the owner chose to include it. It is retired in stage E,
   coupled to the angle-unit migration that makes its degree-based API
   collapse cleanly.
3. **Model point storage stays integer — confirmed as recommended.**
   `iIMDShape::points` remain `iVector` (the model format and loader are
   game data, per Phase 8), loaded per-vertex with `XMLoadSInt3`. Float
   storage plus `XMVector3TransformStream` stays a measured follow-up if
   the vertex loop ever shows up in a profile.
4. **Temporary shims during B–C — confirmed as recommended.** They exist
   for the life of two stages, shrink monotonically, and none survives
   stage D. The alternative was one atomic change across 23 files.
5. **Angle state migrates to float radians — ruled beyond the
   recommendation.** The plan proposed keeping binary angles and degrees
   in the object structs, converting at the render boundary; the owner
   chose the full migration. Stage E carries it: the fields, the wrap
   arithmetic, and the three integer-degree boundaries (level readers,
   net wire, nothing else) are inventoried
   [above](#the-angle-units-in-game-state--migrating-by-decision-5).
6. **`PIEVECTORF` → `XMFLOAT3` at full scope — confirmed as
   recommended**, the type swap landing first and the hand-rolled
   arithmetic in `WarCAM`/`Atmos`/`Effects` moving onto `XMVECTOR` ops
   only where the expression genuinely is a vector op.

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

Stage E's figures: the angle-field counts are `grep -rIoE` of
`(->|\.)(direction|pitch|roll|turretRotation|turretPitch)\b` over all
three code projects, tallied per field; the `DEG(` counts are `grep -c`
per file. The three boundary claims were checked rather than assumed: the
level readers at `Game.cpp:1059/1274/1451` (casting on-disk `UDWORD
direction` into the fields), the wire packing in `MultiSync.cpp` (`NetAdd2`
of `direction`, reconciliation via `dir % 360`), and a grep of
`Outpost/Script*.cpp` finding no angle field exposed to the script VM.
`Trig.cpp`'s callers and its `Window.cpp:377/470` init/shutdown sites were
enumerated the same way.
