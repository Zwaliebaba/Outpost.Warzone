# Migrating `.pie` models to NMO — survey, plan and open issues

How the 516 shipped `.pie` models become [NMO](NeuronMeshObject.md), what the
right shape of that migration is, and — the part that matters most — the
things that will go wrong if they are not decided first.

**Status: investigation and plan (2026-08-27). Nothing is converted.** Every
number below was measured against the tree, mostly by
[`tools/pie_to_nmo.py`](../tools/pie_to_nmo.py), a **prototype converter**
written to test whether this plan is executable at all. It is: it converts all
516 files today. It is not the production converter — §8 says what it still
lacks — but it means the numbers here are observations rather than estimates.

Reproduce them with:

```
python tools/pie_to_nmo.py --report
```

**The headline recommendations**, argued below:

1. **One `.pie` file becomes one `.nmo` file.** Do *not* bake multi-part
   buildings into single models: 88 of the 167 models a structure can use are
   shared between structures, and `blguardr.pie` alone serves 13 of them (§6).
2. **Composition stays in the stats data, where it already lives** — but
   becomes explicit, naming a *marker* instead of relying on connector index 0
   (§6.3).
3. **The build phase needs no new mesh data at all.** It is a render mode
   applied to the model the structure already has; what it needs from NMO is
   the *submesh name* saying which part is the base plate (§7).
4. **Convert the `.ani` system in the same motion or not at all.** The meaning
   of a multi-level `.pie` lives in the `.ani`, not in the `.pie` (§5.1); a
   converter that ignores animation silently produces wrong models.
5. **Do it as one owned phase**, converter and loader and data together, per
   [AssetPipeline.md](AssetPipeline.md)'s rule about format replacement. A
   half-migration leaves two model paths in the renderer, which is exactly
   what Phase 8 spent its budget removing.

---

## 1. What actually ships

516 `.pie` files, 570 KB, all `PIE 2`. Parsed in full:

| Measure | Value |
|---|---|
| Files | 516 (all version 2; the loader's version 1/3/4 branches serve no shipped data) |
| `TYPE` values | `0x200` (509 files), `0x220` (7) |
| Levels per file | 1 (506 files), 3 (4), 4 (4), 6 (2) |
| Points / polygons | 16,487 / 7,121 |
| Polygon shapes | 5,634 quads, 1,487 triangles — **79 % quads** |
| Largest level | 243 points, 123 polygons |
| Highest vertex index | 242 — `u16` indices are permanently sufficient |
| Files with `CONNECTORS` | 118, holding 136 connectors (102 files have 1, 14 have 2, 2 have 3) |
| Polygons with texture animation | 1,693 across 203 files |
| …of which 8-frame | **1,543 — the player count, i.e. team colour, not animation** |
| Files with a `BSP` block | 63 |
| Texture page spellings | 37, folding to 21 distinct pages case-insensitively |

Polygon flags, decoded against `NeuronClient/RendMode.h`:

| Flag | Meaning | Polygons |
|---|---|---|
| `0x200` `PIE_TEXTURED` | UVs present | 7,121 (all) |
| `0x800` `PIE_COLOURKEYED` | colour-key transparency | 2,116 |
| `0x2000` `PIE_NO_CULL` | two-sided | 987 |
| `0x4000` `PIE_TEXANIM` | atlas frames follow | 1,693 |
| `0x10000` `PIE_BSPFRESH` | created by the BSP splitter | 449 |

What the prototype produces from all of it:

| Output | Value |
|---|---|
| NMO meshes | 544 (516 files; the 9 frame-animated ones expand to one mesh per frame) |
| Submeshes | 1,135 — 2.2 per file, one per distinct render state |
| Triangles / vertices | 12,668 / 22,143 |
| Markers | 136 — every connector, none lost |
| Materials with an atlas | 549 |
| Bytes | 570 KB `.pie` → 1.83 MB `.nmo` (×3.21) |

## 2. How a building is actually made today

This is the part that decides the plan, so it is worth stating precisely. A
structure is drawn from up to four separate `.pie` files, composed at draw
time in `Outpost/Display3D.cpp:1753-1900`:

```
base plate      structures.json  "baseModel"      drawn flat, terrain-conformed
   +
structure body  structures.json  "model"          the thing that rises as it builds
   +                                              its CONNECTORS[0] is the turret mount
mount           Sensor/ECM/Weapons "mountModel"   drawn at that connector, rotated by turretRotation
   +                                              its own CONNECTORS[0] chains onward
turret          Sensor/ECM/Weapons "model"        drawn there, pitched by turretPitch
   +
muzzle flash    Weapons "muzzleModel"             at the turret's CONNECTORS[0], when firing
```

Measured over the 128 structures: 11 use one model, 14 use two, 23 use three,
**80 use four**.

**The radar tower, worked through** — the example that prompted this
investigation:

| Part | File | Role |
|---|---|---|
| Structure | `BLGUARDR.pie` | the tower. 24 points, `CONNECTORS 1` at `0 0 113`, just above its 111-unit height |
| Mount | `TRMECM2.PIE` | the rotating cradle, drawn at that connector under `turretRotation` |
| Sensor | `GNMSNSR2.PIE` | the dish, drawn at the mount's own connector under `turretPitch` |

So "the radar building with a rotating radar on top" is three files and two
nested attachment points, and the rotation is a *game* value
(`psStructure->turretRotation`, driven by sensor logic) applied to a matrix —
not animation data in any file. That distinction runs through everything
below.

Two more composition mechanisms exist:

- **Capacity modules.** Factories, research facilities and power generators
  swap their body model as modules are added
  (`Outpost/Structure.cpp:2125-2219`, `factoryModuleIMDs` and friends). The
  filenames are derived by **mutating a character of the name**:
  `GfxFile[length] = '0' + module` (`Outpost/Structure.cpp:492`), with a
  `Neuron::Fatal` if the file is missing. A naming convention enforced by
  string surgery is a migration hazard in its own right (§5.9).
- **Multi-level files.** Ten `.pie` files carry more than one `LEVEL`. Their
  meaning is *not in the file* — see §5.1, the single most important issue on
  this list.

## 3. The mapping

| `.pie` | NMO | Notes |
|---|---|---|
| File | Mesh (usually one) | §5.1 decides when it is several |
| `TEXTURE` page | `Material.textures[0]` | one page per file, so one page per mesh |
| `LEVEL` (sub-object) | Submesh group + a mesh bone | §5.1 |
| `LEVEL` (animation frame) | A whole Mesh | §5.1 |
| `POINTS` | `Vertex.position` | integers become floats, unscaled |
| `POLYGONS` indices | Triangles + facet ids | fan-triangulated; facet id records the source polygon (§5.3) |
| Per-polygon UVs | `Vertex.uv` | divided by page size; vertices split per (point, uv) |
| `PIE_COLOURKEYED` / `PIE_NO_CULL` | `MaterialExt.renderFlags` | submeshes split by state (§5.4) |
| `PIE_TEXANIM` params | `MaterialExt` atlas fields | frame count, tile size, selector (§5.4) |
| `CONNECTORS` | `Marker` named `Connector00`… | with the axis swap of §5.2 |
| `BSP` block | *dropped* | dead since Phase 8 (§5.8) |
| `.ani` `"trans"` object | Mesh bone + SRT clip + submesh alias tables | §5.1 |
| `.ani` `"frames"` states | Mesh selection at runtime | §5.1 |
| *(nothing)* | `Vertex.normal` | generated; PIE has none (§5.5) |
| *(nothing)* | `SubMeshFlags::DeformedAtRuntime` | set by hand for base plates (§5.6) |

## 4. Naming, which is a contract

Names are how the engine will find things after the migration, so the
converter has to establish them deliberately rather than by accident.

- **Markers**: `Connector00`, `Connector01`, … in file order, so the existing
  index-based call sites migrate mechanically (connector *i* → `Connector0i`).
  Renaming to `TurretMount` / `Muzzle0` happens per call site afterwards, not
  in the same change.
- **Submeshes**: the `.ani` object name when there is one (`DerrickBase`,
  `Piston`, `Box03` — these already exist in the data and are already
  meaningful), otherwise `<file>.<n>` for a state split. For structures, the
  role names the engine will look for are **`Base`** (drawn unscaled, terrain
  conformed) and **`Body`** (height-scaled while building) — see §7.
- **Meshes**: the `.pie` stem. Frame expansions get `<stem>.frameNN`.
- **Case**: `GameData/` mixes `BLDerik.PIE`, `blderik.pie` and `BLDERIK.PIE`
  freely, and the game resolves case-insensitively on NTFS
  (`tools/validate_assets.py` already treats a case-only match as a warning).
  The converter must emit one canonical spelling and the stats data must be
  rewritten to match, or the Linux checkers will fail on files Windows loads
  (§5.10).

## 5. Issues to cover

Ordered by how much damage each does if missed.

### 5.1 A multi-level `.pie` does not say what its levels mean

**The problem.** `LEVELS 3` can mean two completely different things:

- **Animation frames** — the whole shape is replaced per frame
  (`ANIM_3D_FRAMES`).
- **Animated sub-objects** — each level is a *part*, moved by its own
  position/rotation/scale track (`ANIM_3D_TRANS`).

Nothing in the `.pie` distinguishes them. The answer lives in the `.ani` that
references the file, in its `"type"` field. All ten multi-level files are
covered:

| `.pie` | Levels | Anim type | Becomes |
|---|---|---|---|
| `walkanim`, `RunAnim`, `RunFlame`, `FlamFall` | 4 | `frames` | 4 meshes each |
| `cybd_run`, `cybdprun` | 6 | `frames` | 6 meshes each |
| `FireKnee`, `cybdpjmp`, `cybdplnd` | 3 | `frames` | 3 meshes each |
| **`BLDerik`** | 3 | **`trans`** | **1 mesh, 3 animated sub-objects** |

The evidence that these really are different: the `frames` files have
*different topology per level* (`FireKnee` is 18, 23, 18 points), which only
makes sense as replacement; `BLDerik`'s levels are named parts
(`DerrickBase`, `Piston`, `Box03`) with 25 keyframes each.

**Resolution.** The converter takes `--anims` and refuses to convert a
multi-level file with no entry there, rather than guessing. The prototype
implements exactly this; if a new multi-level model appears without an
animation, conversion stops and a human decides.

**The consequence for the plan:** the `.ani` system and the model format have
to move together. Converting models first and animation later means writing a
converter that cannot know what it is looking at.

### 5.2 `.pie` uses two different axis conventions in the same file

**The problem.** Points are `(x, y-up, z)`. Connectors are `(x, y, z-up)` —
the third component is the height. The renderer swizzles at every use site:

```cpp
// Outpost/Display3D.cpp:1837
XMMatrixTranslation(strImd->connectors->x, strImd->connectors->z, strImd->connectors->y)
```

**The evidence, because a convention this odd deserves proof.** Across the
corpus, 75 connectors have a third component inside their model's height range
and a second component outside it; **zero** have the reverse. `BLGUARDM.PIE`
spans y ∈ [0, 115] and its connector is `0 0 116`. `BLhardpt.PIE` spans
y ∈ [0, 13] with connector `0 0 14`.

**Resolution.** The converter swaps Y and Z for connectors only, and NMO has
one space for everything (`NeuronMeshObject.md` §6). Every marker in the
converted corpus was checked to land inside or just above its model's bounds.

**Watch for:** anyone re-authoring a model in Blender and hand-copying an old
connector value will get it wrong. The add-on avoids this by only ever seeing
NMO coordinates.

### 5.3 79 % of shipped polygons are quads, and NMO stores triangles

**The problem.** Triangulation is lossy for *editing*: an artist opening a
converted building sees a triangle soup where the original had clean quads,
and re-quadifying by heuristic gets the diagonals wrong on any non-planar face.

**Resolution.** NMO carries an optional `uint32` **facet id** per triangle
naming its source polygon (`NeuronMeshObject.md` §4.6). Triangles with equal
ids were one polygon; a tool rebuilds it exactly. Cost is 4 bytes × 12,668
triangles = 51 KB across the entire game. The Blender add-on imports facet ids
as a face attribute and regenerates them on export, so the information
survives editing rather than only the first conversion.

**Still to decide:** whether the Blender importer should *automatically*
re-join facets into quads on import (friendlier, but then export must
re-triangulate identically to keep round-trips byte-exact) or leave them as
triangles with the attribute present (what it does now). Recommend leaving it
manual until an artist has actually used the tool.

### 5.4 "Texture animation" is mostly team colour

**The problem — and the most easily-missed thing in this document.** 1,693
polygons carry `PIE_TEXANIM`, and it looks like animation. It mostly is not:

```cpp
// NeuronClient/RenderModel.cpp:121-122
if (frame == 0)
  frame = team;
```

The frame index and the owning player's index are the *same parameter*.
1,543 of the 1,693 animated polygons declare exactly 8 frames — the player
count — and every single animated polygon in the corpus declares
`playbackRate == 1`. So the dominant use of the feature is: *pick the tile of
the atlas belonging to this player's colour.*

Miss this and every converted building loses its team colour, which will look
like a texture bug rather than a format decision.

**Resolution.** `MaterialExt` carries the atlas description and an explicit
`atlasSelector` — `Team` or `Time` — so the two uses stop being the same
field. The converter reads it as `Team` at 8 frames and `Time` otherwise; that
heuristic is right for the shipped data but should be checked against art
intent before it becomes permanent, which is the one open question left here.

**Also resolved by the same change:** the per-polygon UV recomputation the
renderer does every frame (`RenderModel.cpp:552-579`, with its `// HACK - fix
this!!!!`) becomes a per-draw-call constant, because faces sharing atlas
parameters are now one submesh.

### 5.5 `.pie` has no normals, and adding them changes how models look

**The problem.** The format stores no vertex or face normals. The loader
computes face normals and never reads them (recorded in
[AssetPipeline.md](AssetPipeline.md) §4); shading today is a per-object light
level, flat across the model. NMO's `Vertex` has a normal, so the converter
must invent one — and if the renderer ever starts using it, every model's
appearance changes at once.

**Resolution.** Generate area-weighted smoothed normals (the prototype does),
because that is what a modern shading path wants and what Blender will show.
But treat "the renderer starts lighting per-vertex" as a *separate, visible*
change with its own before/after screenshots — not something that rides in on
a format migration. Until then the normals are carried and ignored, exactly as
they are today.

### 5.6 Some meshes have their vertices rewritten every frame

**The problem.** Base plates are conformed to terrain by mutating the shape's
point array in place before drawing (`Outpost/Display3D.cpp:2337-2352`), and
`flattenImd` does the same for the body. Electronic damage jitters points the
same way (`Display3D.cpp:1764-1775`). A loader that uploads every submesh into
a static vertex buffer breaks all of this silently — the buildings simply stop
sitting on the ground.

Worse, the mutation happens on the *shared* `iIMDShape`, so two structures
using the same model on different terrain are writing over each other within a
frame. That is a pre-existing defect, not one the migration creates, but the
migration is when it becomes visible.

**Resolution.** `SubMeshFlags::DeformedAtRuntime` marks these; the loader
keeps a writable copy for them and static buffers for everything else. The
converter cannot infer the flag — it is a property of how the *game* uses the
model, not of the model — so it comes from a small table of role names (§7),
which is another reason submesh names are in the format.

### 5.7 Texture page names contain spaces, vary in case, and point at `.pcx`

**The problem.** `TEXTURE 0 page-9-player buildings-bases.pcx 256 256` — the
file name contains a space, which breaks any whitespace tokenizer (it broke
the first version of the survey script). Names also vary in case: 37 distinct
spellings fold to 21 actual pages. And they still say `.pcx`, a format the
palette work replaced with `.dds` (`NeuronClient/Dds.cpp`;
`Pcx.cpp` was deleted).

**Resolution.** Parse the `TEXTURE` line by line, not by token (the prototype
does). Normalize to lowercase and rewrite the extension to `.dds` at
conversion. Keep the space — NMO strings are UTF-8 and the resource system
resolves the name — but decide once whether the canonical name keeps it.

### 5.8 63 files carry a dead `BSP` block

Parsed, allocated and never traversed since the BSP renderer was deleted in
Phase 8 stage A. Its rows are variable width and it can appear *before*
`CONNECTORS`, so a parser that stops at `BSP` silently loses 17 connectors —
which the first survey pass did, reporting 119 instead of 136.

**Resolution.** Drop it, and make sure the parser skips it correctly rather
than abandoning the file. `PIE_BSPFRESH` (449 polygons) is likewise a runtime
artefact of the splitter and carries no meaning in NMO.

### 5.9 Module models are found by mutating a filename character

`Outpost/Structure.cpp:492` builds each capacity module's filename by writing
a digit into a fixed position of the previous name, and calls `Neuron::Fatal`
when the result does not exist. Any renaming scheme the migration adopts must
either preserve that exact character position or replace the mechanism with an
explicit list in the stats data. **Recommend replacing it** — the migration is
touching those stats anyway (§6.3) — but it must be a deliberate step, because
the failure mode is a fatal error at load on a file nobody noticed was
name-derived.

### 5.10 Case-insensitive name resolution

The stats data spells model names inconsistently (`BLFACT0.pie` vs
`blbfact.pie` vs `BLDERIK.PIE` for the same files on disk with a third
spelling). The game resolves case-insensitively; `tools/check_case.py` and
`tools/validate_assets.py` exist because that hides real errors. Converting
gives one chance to canonicalise every model name in one commit — take it, and
extend `validate_assets.py` to make case-exact `.nmo` references an error
rather than a warning.

### 5.11 Degenerate polygons

15 files contain polygons with a repeated vertex index; the prototype drops 55
triangles that collapse to a line. They are almost certainly authoring noise,
but the count should be reviewed by eye before the conversion is accepted —
one of them might be a quad that should have been a triangle, in which case
dropping it leaves a hole.

### 5.12 Winding and culling need a visual check, not a proof

The renderer culls by testing screen-space winding after transform
(`Clockwise(poly->pVrts)`, `RenderModel.cpp:512`) rather than trusting a
declared order, and `PIE_NO_CULL` opts 987 polygons out. The converter
preserves index order, which *should* be right — but "should" is not a
verification. This is the one item on the list that only a running game can
settle: build a level, look at the buildings, check nothing is inside-out.

### 5.13 The files get 3.2× bigger

570 KB → 1.83 MB. Expected — 52-byte float vertices against compact integer
text — and irrelevant next to `GameData/`'s 520 MB of media, but it should be
stated rather than discovered. `NeuronMeshObject.md` §8 lists the packing
options that would claw it back if it ever matters; none is worth doing now.

### 5.14 What happens to `.ani`

Ten animation files, of which one (`BLDerik`) is non-trivial and two ship
without ever playing. After migration, the `trans` animation is *inside* the
model and `anim.json`'s ID table, `Outpost/AnimID.h`'s hardcoded IDs and the
positional binding between them all become dead weight. The `frames`
animations become "select mesh N of this NMO", which is a runtime rule, not
data.

Retiring that system is most of the value of this migration and should be
scoped into it explicitly — otherwise the engine ends up with animation in two
places, which is worse than having it in the old one.

## 6. Multi-part buildings: do not merge them

### 6.1 The question

Given NMO can hold several submeshes with their own skeletons and clips,
should the radar tower become one `.nmo` containing tower + mount + dish, with
the dish as an animated submesh?

### 6.2 The answer: no, and the reuse numbers say why

167 distinct models are referenced by the 128 structures. **88 of them are
used by more than one structure:**

| Model | Structures using it |
|---|---|
| `trlsnsr1.pie` | 18 |
| `gnlsnsr1.pie` | 15 |
| `blguard1.pie` | 14 |
| `blguardr.pie` | 13 |
| `blhardpt.pie` | 12 |

The composition is combinatorial: *n* towers × *m* turrets × *k* mounts, and
the data expresses it as *n + m + k* files. Baking would turn that back into
*n × m × k* — every tower duplicated per turret it can carry — multiplying
both the data and the number of places a fix has to be applied. It would also
be the first thing an artist swears at: changing one tower would mean editing
thirteen models.

**The decision rule**, for the cases where folding *is* right:

| Situation | Do this |
|---|---|
| Part is used by more than one parent | Separate `.nmo`, attached at a marker |
| Part is exclusive to one parent **and** needs its own animation timeline | Submesh in the parent, with its own bone table and clips |
| Part is exclusive and static | Either; prefer a submesh, fewer files |
| Part is a whole-mesh animation frame | A mesh inside the same `.nmo` |

79 of the 167 are used by exactly one structure, so the second and third rows
are not hypothetical — but folding them is an optimisation to do *later*, per
model, with a reason. The mechanical migration should be 1:1.

### 6.3 What composition should become

It already lives in the stats JSON (`structures.json` `model`/`baseModel`,
`Sensor.json` `model`/`mountModel`, `Weapons.json` `model`/`mountModel`/
`muzzleModel`). Keep it there, and make two things explicit that are currently
implicit in code:

1. **Which marker** a part attaches to. Today it is always
   `connectors[0]`, and `Display3D.cpp` will not draw a turret at all unless
   `nconnectors == 1` — with a separate branch treating `nconnectors > 1` as
   "landing lights". Naming the marker (`"attachTo": "TurretMount"`) removes
   the coincidence.
2. **Which submesh rotates**, for parts folded into a parent. A name, not an
   index.

That is a small, additive change to files the migration is already rewriting,
and it is the thing that makes NMO's markers useful rather than decorative.

### 6.4 Worked: the radar tower after migration

```
BLGUARDR.nmo      3 submeshes (split by render state), 1 marker "Connector00" at (0, 113, 0)
TRMECM2.nmo       the mount, its own marker for the next attachment
GNMSNSR2.nmo      the dish
structures.json   Sys-VTOL-RadarTower01 -> model BLGUARDR.nmo
Sensor.json       Sys-VTOLRadarTower01  -> model GNMSNSR2.nmo, mount TRMECM2.nmo,
                                           attachTo "Connector00"
```

The rotation stays exactly where it is now: a game-driven matrix at the
marker. Nothing about "a rotating radar on top" needs animation data in a
file — and it should not acquire any, because the rotation tracks a target.

*(This was verified end to end: `BLGUARDR.PIE` converts, loads into Blender
with its marker at the right height, exports again and re-reads with all 30
triangles intact.)*

## 7. The build phase

**What it is today.** A structure under construction is drawn with the *same*
model, height-scaled by its build progress:

```cpp
// Outpost/Display3D.cpp:1783
pie_Draw3DShape(imd, 0, playerFrame, brightness, specular, pie_HEIGHT_SCALED,
                structHeightScale(psStructure) * pie_RAISE_SCALE);
```

`structHeightScale` is `currentBuildPts / buildPoints` clamped to ≥ 0.05
(`Outpost/Structure.cpp:7545`), and `pie_HEIGHT_SCALED` multiplies each
vertex's positive Y by it (`RenderModel.cpp:206-210`). The base plate is drawn
*unscaled* alongside, so the structure appears to rise out of its foundation.

**What this means for the migration — and it is good news.** There is no
"build phase mesh". No separate model, no LOD, no partial geometry. What the
renderer needs is:

1. The model — already there.
2. The knowledge that the base plate is not scaled and the body is.

Point 2 is the only thing NMO must carry, and it does, as a **submesh name**
(`Base` vs `Body`) that the engine maps to behaviour at load. Deliberately not
a format flag: "gets height-scaled while building" is a game rule about
structures, and a mesh format that encodes game rules is a mesh format that
needs a new field every time a rule changes.

**Two things to check when this is implemented:**

- Base plates and bodies are separate *files* today, so after a 1:1 migration
  they are separate `.nmo`s and the naming rule applies across files rather
  than within one. If a later pass folds a base plate into its body as a
  second submesh (§6.2 row 3 permits it), the names start doing real work.
- The base plate is also the terrain-conformed one, so it wants
  `DeformedAtRuntime` (§5.6). Same name, two consequences — worth setting both
  from one table rather than two.

## 8. What the prototype does not do

Being explicit about the gap between "this plan is executable" and "this plan
is executed":

- **No tangents.** `Vertex.tangent` is written as a constant. Nothing consumes
  it; when something does, generate it properly.
- **The `Time`/`Team` atlas split is a heuristic** (frame count == 8). Right
  for the shipped data; should be confirmed against art intent (§5.4).
- **No constant-track elimination**, so `BLDerik`'s clip carries 225 keys where
  a third would do (`NeuronMeshObject.md` §8.4).
- **Flat output directory**, no `GameData/` tree mirroring, no stats rewriting.
- **No `DeformedAtRuntime` marking** — it needs the role table of §7.
- **No visual verification of anything**, which is §5.12's point and the whole
  of §9.

## 9. Staged plan

Each stage is independently reviewable and leaves the tree working. Stages A–C
change no shipped data.

| Stage | Work | Gate |
|---|---|---|
| **A** | `NeuronClient/Nmo.h` — the §4 structs, no logic. `NeuronClientTest` gets the size and layout assertions. | Builds Win32 + x64, `check_case.py` green |
| **B** | `NeuronClient/NmoLoad.cpp` — validation (`NeuronMeshObject.md` §4.11) and in-place load. Tests get the golden fixture and every malformed case the Python tests already cover. | Tests pass; no renderer change yet |
| **C** | Production converter: `GameData/` tree mirroring, stats rewriting, role table, canonical naming. Corpus conversion is reproducible and diffable. | 516/516 convert; every converted file loads through stage B |
| **D** | Renderer draws NMO behind a flag, `.pie` path untouched. One campaign level converted. | **Run the game.** `Debug\Outpost.exe -window -game CAM_1A`, compare screenshots against the `.pie` path: winding, team colour, terrain conforming, build-in-progress, turret placement |
| **E** | Convert all data, retire the `.pie` loader and the `.ani` system, delete `IMDLoad.cpp` and friends. | Full campaign run; `validate_assets.py` green on `.nmo` |

**Stage D is the real gate.** Everything before it is checkable by machine;
the things this migration can plausibly get wrong — inside-out polygons, lost
team colour, floating turrets, buildings that no longer sit on the ground — are
all things you find by looking at the screen. Per
[AGENTS.md](../AGENTS.md) §3: a green build says nothing about whether the game
draws.

## 10. Risks

| Risk | Why it bites | Mitigation |
|---|---|---|
| Team colour lost | Looks like a texture bug, not a format decision; 203 files affected | §5.4; check a two-player skirmish at stage D |
| Buildings stop conforming to terrain | Silent; only visible on sloped ground | §5.6; stage D on a hilly level specifically |
| Multi-level file misread | Wrong model entirely, or a static derrick | §5.1; converter refuses to guess |
| Connector axis swapped | Turrets float or sink into towers | §5.2; verified across the corpus, re-verify by eye |
| Two model paths left in the tree | Exactly what Phase 8 removed at cost | Stage E is not optional; scope it in from the start |
| `.ani` retired halfway | Animation in two places | §5.14; same phase or not at all |

## 11. Recommendation

Do it, as one phase, in the order above, with the `.ani` system inside the
scope — and do not start it while Phase 8's renderer collapse or Phase 10's
maths migration is still in flight, because stage D needs a stable renderer to
compare against. The format is ready, the converter is proven executable on
the whole corpus, and the editing story exists. What is missing is a decision
to own the change end to end, which is the same thing
[AssetPipeline.md](AssetPipeline.md) concluded about every other format in this
tree.
