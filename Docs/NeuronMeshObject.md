# Neuron Mesh Object (NMO): Format Design

A design proposal for **NMO**, a binary model format for the Neuron engine. It
is derived from Microsoft's **CMO** (Compiled Mesh Object) format — the layout
documented in
[`DirectXTK/Src/CMO.h`](https://github.com/microsoft/DirectXTK/blob/main/Src/CMO.h)
(© Microsoft Corporation, MIT license, produced originally by Visual Studio's
`MeshContentTask` for the VS Direct3D Starter Kit) — with two required
extensions and a set of container-level corrections that CMO's own structure
makes necessary.

**Status: design only (2026-08-27).** No loader, no converter and no `.nmo`
file exists in the tree, and none is added by this document.
[Phase8Plan.md](Phase8Plan.md) deliberately fenced the model format out of the
renderer collapse ("the `.pie`/IMD model format and its loader are game data
and are not touched"), and [AssetPipeline.md](AssetPipeline.md) §4 ruled a
`.pie` text-to-JSON conversion out because the wins are loader-side. This
document is the groundwork for that later loader-side phase. It aligns with
[Phase10Plan.md](Phase10Plan.md), which commits the renderer to native
DirectXMath (`XMFLOAT3`/`XMFLOAT4X4` storage): every on-disk vector and matrix
below is a DirectXMath storage type.

**Requirements**, as given:

- **R-NMO-1** — Bone animation, *as defined by CMO* (a named-bone table plus
  named clips of keyframes), must be possible **per submesh**, not only per
  mesh.
- **R-NMO-2** — Each submesh carries a list of **markers**: named points, each
  with an `XMFLOAT3` position.
- **R-NMO-3** — Analyze the proposal; state best practice and the
  optimizations it should adopt or defer. §2 (what CMO gets wrong), §3.3 (the
  delta table), §5–§7 (semantics and best practice) and §8 (deferred
  optimizations, with numbers) answer this.

**Non-goals of v1.0:** scene graphs, cameras, lights (CMO has none either),
animation curves (converters bake to keys), LOD policy, compression, and any
runtime blending/priority policy — the file format defines *data*; how clips
mix at runtime is an engine decision and deliberately not encoded here.

---

## 1. Why these two extensions are the right ones for this engine

Both requirements have direct ancestors in the tree, which is good evidence
they are load-bearing rather than speculative:

- **Per-submesh animation** exists today in primitive form. The `.ani` system
  (`ANIM_3D_TRANS`, [Anim.h](../NeuronClient/Anim.h)) animates *sub-objects* of
  a droid or structure with per-state position/rotation/scale — a turret
  spinning on a hull, the Derrick pumping. That is submesh-scope animation
  expressed as external scripts gluing separate `.pie` files together. NMO
  moves it inside the model, expressed with CMO's own bone/clip structures.
- **Markers** are the successor of IMD **connectors**
  (`iIMDShape.nconnectors` / `connectors` in
  [Model.h](../NeuronClient/Model.h)): the mount points the game uses for
  turret placement and muzzle effects, today addressed by bare index with
  hard-coded meanings. NMO gives them names and puts them on the submesh they
  belong to.

CMO itself is the right base: it is a small, documented, flat binary layout
with exactly the feature set this engine needs (multi-mesh, materials,
submeshes over shared buffers, skinning, named clips), it has a reference
loader in DirectXTK, and its structures are plain `DirectXMath` PODs — the
direction Phase 10 is already taking the renderer.

## 2. CMO in brief — what is inherited, what is corrected

### 2.1 The CMO layout (summary)

A `.cmo` file is `UINT meshCount` followed by that many mesh blocks. Each mesh
block is, in stream order: name; materials (name, `Material`, pixel-shader
name, 8 texture names); `BYTE` skeletal-data flag; `SubMesh[]`; index buffers
(`USHORT` only); vertex buffers; skinning vertex buffers; `MeshExtents`; and —
only if the flag byte was 1 — `Bone[]` (named) and animation clips (named
`Clip` + `Keyframe[]`). All strings are length-prefixed `wchar_t`. All structs
are `#pragma pack(1)`:

| CMO struct | Size | Content |
|---|---|---|
| `Material` | 132 | ambient/diffuse/specular/emissive (`XMFLOAT4`), specular power, `XMFLOAT4X4` UV transform |
| `SubMesh` | 20 | materialIndex, indexBufferIndex, vertexBufferIndex, startIndex, primCount |
| `Vertex` | 52 | position, normal, `XMFLOAT4` tangent, `uint32` color, UV |
| `SkinningVertex` | 32 | `uint32 boneIndex[4]`, `float boneWeight[4]` |
| `MeshExtents` | 40 | sphere center+radius, AABB min/max |
| `Bone` | 196 | parentIndex, `InvBindPos`, `BindPos`, `LocalTransform` (all `XMFLOAT4X4`) |
| `Clip` | 12 | start time, end time, key count |
| `Keyframe` | 72 | boneIndex, time, `XMFLOAT4X4` transform |

**Kept, deliberately:** the two-stream vertex layout (skinning data in a
*separate* parallel buffer, so rigid meshes never pay for it and the static
render path binds one stream); per-mesh extents; the material model; the
`Bone`/`Clip`/`Keyframe` structures themselves (R-NMO-1 says "as defined in
CMO"); size `static_assert`s on every on-disk struct.

### 2.2 What CMO gets wrong — each of these shaped the NMO container

1. **No magic number, no version, no sizes.** A `.cmo` begins directly with a
   mesh count; the only way to know a file is not a CMO is to crash parsing
   it, and the format cannot evolve at all.
2. **`wchar_t` strings.** UTF-16 doubles the size of every ASCII name, and
   `wchar_t` is 2 bytes on Windows and 4 on the Linux cross-checkers this
   repository runs — exactly the class of defect
   [X64Readiness.md](X64Readiness.md) exists to keep out of files.
3. **A byte-stream with `#pragma pack(1)`.** A lone `BYTE` flag mid-stream and
   odd-length strings destroy alignment for everything after them, so nothing
   can be read in place; every load is a field-by-field copy.
4. **Sequential-only.** Nothing can be skipped, validated up front, or loaded
   selectively; a reader must understand every byte to reach the next mesh.
5. **16-bit indices only.** A hard 65,535-vertex ceiling per buffer.
6. **Matrix keyframes.** 64 bytes of `XMFLOAT4X4` per key, and linearly
   interpolating matrices shears — the correct runtime wants
   translation/rotation/scale keys (§8.1). v1.0 keeps CMO keyframes for
   fidelity; the container leaves room to replace them.
7. **Unspecified orderings.** CMO does not promise keyframe order, bone
   topological order, or how skinning buffers pair with vertex buffers; every
   loader re-discovers or re-sorts. NMO specifies all three (§3.1).
8. **Skeleton and clips are mesh-scope only**, and there are **no attachment
   points** — the two requirements.

## 3. NMO design overview

### 3.1 Container rules

These rules govern the whole format; the structures in §4 are their
consequence.

- **Little-endian, fixed-width fields only** (`uint32_t`, `int32_t`, `float`,
  `uint16_t`). No `wchar_t`, no pointers, no size-dependent types on disk
  ([X64Readiness.md](X64Readiness.md) compliance by construction).
- **Natural alignment, no packing pragmas.** Every struct is designed so its
  MSVC/GCC layout is identical with default packing, proven by
  `static_assert`. Sections start 16-byte aligned; strings pad to 4. The
  payload can be read with one `fread` and used in place.
- **Presence is a count, never a flag byte.** CMO's `BYTE` skeletal flag
  becomes `boneCount > 0`. No lone bytes exist anywhere in the stream.
- **Variable-size data is reached through offsets.** The file header carries a
  mesh directory; each mesh header and each submesh record carries
  byte-offsets (mesh-relative) to its sections. Offsets are authoritative;
  §4.12 gives the recommended physical order. `0` means "absent". Records may
  be shared: two submeshes may point at the same marker records.
- **Bulk data is fixed-stride; only named records vary.** Vertices, indices,
  skin vertices, keyframes, `SubMesh` and `MeshRef` tables are stride-exact
  arrays usable in place. Only the small, parsed-once record streams
  (materials, bones, clips, markers) contain strings.
- **Specified orderings.** Keyframes sort by (boneIndex, time); bones are
  topologically ordered (`parentIndex < ownIndex`); skin buffer *i* pairs
  vertex buffer *i*. Loaders never sort.
- **Strings are length-prefixed UTF-8**, not zero-terminated, padded with
  zeros to the next 4-byte boundary, at most `MaxStringBytes` (1024).
- **Reserved means zero on write, ignored on read.** That single rule is the
  minor-version mechanism (§4.11).
- **Validation before trust** (§4.10). A loader rejects; it never repairs.

### 3.2 Layout at a glance

```
file.nmo
├─ FileHeader                32 B   magic 'NMO1', version, sizes, CRC
├─ MeshRef[meshCount]         8 B   file offset + length of each mesh blob
└─ mesh blob, 16-byte aligned, one per mesh:
   ├─ MeshHeader             96 B   counts + offsets of everything below
   ├─ String                        mesh name
   ├─ material records   × materialCount   name, Material, shader, 8 texture names
   ├─ SubMesh[subMeshCount] 128 B   draw range, extents, offsets to its own:
   │    ├─ bone records   × boneCount     submesh skeleton / bone palette   (R-NMO-1)
   │    ├─ clip records   × clipCount     submesh animation clips           (R-NMO-1)
   │    └─ marker records × markerCount   named XMFLOAT3 points             (R-NMO-2)
   ├─ index buffers      × indexBufferCount    BufferHeader + u16/u32 indices
   ├─ vertex buffers     × vertexBufferCount   BufferHeader + Vertex[]
   ├─ skin buffers       × skinBufferCount     BufferHeader + SkinVertex[]
   ├─ MeshExtents            40 B   mesh bounds
   ├─ bone records       × boneCount    mesh skeleton   (as CMO)
   └─ clip records       × clipCount    mesh clips      (as CMO)
```

### 3.3 CMO → NMO delta table

| CMO | NMO v1.0 | Why |
|---|---|---|
| No identification; parse-or-crash | 32-byte header: magic, major.minor version, sizes, optional CRC-32 | identify, version, validate before trusting a byte |
| `wchar_t` names | length-prefixed UTF-8, 4-aligned, ≤ 1024 B | portable, smaller, x64/cross-checker safe |
| `pack(1)` stream, `BYTE` flags | natural alignment; presence = count; 16-aligned sections | one-`fread` in-place loading, no unaligned reads |
| Sequential-only | mesh directory + per-mesh offset tables | random access, skipping, forward compatibility |
| `USHORT` indices only | per-buffer `u16` (preferred) or `u32` | removes the 64 K ceiling without taxing small meshes |
| Skeleton/clips per mesh only | identical structures also per submesh, plus bone aliasing | **R-NMO-1**; bone palettes for D3D9 constant limits; independent parts (§5) |
| No attachment points | named markers per submesh, optional bone binding | **R-NMO-2**; named successor of IMD connectors (§6) |
| Extents per mesh | extents per submesh as well | cull independently animated parts |
| Draw range = start + primCount | + baseVertex, minVertex, vertexCount | complete `DrawIndexedPrimitive` arguments; range validation |
| Keyframe order unspecified | sorted (bone, time) | the flat list *is* per-bone tracks; binary search; playback cursors |
| Bone order unspecified | parents before children | single forward pass builds a pose |
| Skin-VB pairing implicit | explicit: count is 0 or equals VB count, pair by index | removes loader guesswork |

## 4. Normative specification — NMO version 1.0

All C++ below is the on-disk mirror, target style per [AGENTS.md](../AGENTS.md)
(`namespace Neuron::Nmo`, aggregate fields plain camelCase per R8, units in
names per R6). Types come from `<cstdint>` and `<DirectXMath.h>` — both
Windows-SDK, no new dependency. Every struct is trivially copyable and needs
**no** packing pragma; the `static_assert`s are part of the specification.

### 4.1 Conventions

| Aspect | Rule |
|---|---|
| Byte order | Little-endian throughout. The magic reads `"NMO1"` on disk only when the byte order is right. |
| Coordinates | Left-handed, +X right, +Y up, +Z forward. Clockwise front faces (D3D9 default cull assumptions, matching CMO-in-practice). The converter owns fixing source-tool conventions. |
| Units | Spatial values are world units (unit-agnostic; pipeline policy). Time is **seconds** (`float`), as CMO. UV origin top-left, V down. |
| Primitive | Indexed triangle lists only. |
| Vertex color | Byte order R,G,B,A at ascending addresses (CMO's convention). The D3D9 FVF diffuse path wants `D3DCOLOR` (B,G,R,A); the loader swizzles at upload. |
| Floats | Writers must emit finite values. |
| `String` | `uint32_t lengthBytes` + that many UTF-8 bytes + zero padding to the next 4-byte boundary. Not terminated. `lengthBytes ≤ MaxStringBytes`. Invalid UTF-8 is a load failure. |

```cpp
namespace Neuron::Nmo
{

inline constexpr std::uint32_t FileMagic = 0x314F4D4E;  // "NMO1" on disk
inline constexpr std::uint16_t VersionMajor = 1;
inline constexpr std::uint16_t VersionMinor = 0;
inline constexpr std::uint32_t MaxStringBytes = 1024;
inline constexpr std::uint32_t TextureSlots = 8;        // as CMO
inline constexpr std::uint32_t BoneInfluences = 4;      // as CMO
inline constexpr std::int32_t NoParent = -1;
inline constexpr std::int32_t NoBone = -1;
```

### 4.2 File header and mesh directory

```cpp
struct FileHeader
{
  std::uint32_t magic;         // FileMagic
  std::uint16_t versionMajor;  // breaking changes only
  std::uint16_t versionMinor;  // additive changes only (§4.11)
  std::uint32_t headerBytes;   // sizeof(FileHeader) == 32
  std::uint32_t fileBytes;     // total file size, for validation
  std::uint32_t meshCount;
  std::uint32_t flags;         // 0 in v1.0
  std::uint32_t payloadCrc32;  // CRC-32 (poly 0xEDB88320) of bytes
                               // [headerBytes, fileBytes); 0 = not computed
  std::uint32_t reserved;      // 0
};
static_assert(sizeof(FileHeader) == 32, "NMO FileHeader size");

struct MeshRef
{
  std::uint32_t offsetBytes;   // from file start; 16-byte aligned
  std::uint32_t lengthBytes;   // whole mesh blob
};
static_assert(sizeof(MeshRef) == 8, "NMO MeshRef size");
```

`MeshRef[meshCount]` immediately follows the header. `uint32_t` offsets cap a
file at 4 GB — three orders of magnitude above any model this game will ship,
and it keeps every offset field half the size. `payloadCrc32` is written by
the tool and checked by debug/tool builds only; a release loader skips it.

### 4.3 Mesh header

All offsets in `MeshHeader` and `SubMesh` are **relative to the mesh blob's
first byte** and must be 16-byte aligned for buffer sections, 4-byte aligned
for record streams. `0` = section absent.

```cpp
struct MeshHeader
{
  std::uint32_t flags;               // 0 in v1.0
  std::uint32_t nameOffset;          // -> String; 0 = unnamed
  std::uint32_t materialCount;
  std::uint32_t materialsOffset;     // -> material records, sequential
  std::uint32_t subMeshCount;
  std::uint32_t subMeshesOffset;     // -> SubMesh[subMeshCount]
  std::uint32_t indexBufferCount;
  std::uint32_t indexBuffersOffset;  // -> buffer records, sequential
  std::uint32_t vertexBufferCount;
  std::uint32_t vertexBuffersOffset;
  std::uint32_t skinBufferCount;     // 0, or == vertexBufferCount
  std::uint32_t skinBuffersOffset;
  std::uint32_t extentsOffset;       // -> MeshExtents
  std::uint32_t boneCount;           // mesh skeleton; 0 = none (no flag byte)
  std::uint32_t bonesOffset;         // -> bone records, sequential
  std::uint32_t clipCount;           // mesh clips
  std::uint32_t clipsOffset;         // -> clip records, sequential
  std::uint32_t reserved[7];         // 0; future sections claim these (§4.11)
};
static_assert(sizeof(MeshHeader) == 96, "NMO MeshHeader size");
```

### 4.4 Materials

A material record is, in order: `String` name, `Material`, `String`
pixel-shader name, and exactly `TextureSlots` (8) `String` texture names,
empty slots included — CMO's model, unchanged, including the struct layout:

```cpp
struct Material                      // layout-identical to CMO Material
{
  DirectX::XMFLOAT4 ambient;
  DirectX::XMFLOAT4 diffuse;
  DirectX::XMFLOAT4 specular;
  float specularPower;
  DirectX::XMFLOAT4 emissive;
  DirectX::XMFLOAT4X4 uvTransform;
};
static_assert(sizeof(Material) == 132, "NMO Material size");
```

For this engine's current renderer (texture pages, no shader system) the
converter emits: `texture[0]` = page name, shader name empty, `uvTransform`
identity. The unused Phong fields cost 132 bytes per material and buy
CMO-compatibility and a future; that trade is accepted. Note what this model
does **not** cover: the `.pie` per-polygon frame-based texture animation
(1,693 polygons ship with it — [AssetPipeline.md](AssetPipeline.md) §2). See
§9 and §10.

### 4.5 Buffers

Every buffer — index, vertex, skin — is one uniform record: a `BufferHeader`,
then `strideBytes × elementCount` payload bytes, then zero padding to the next
16-byte boundary. Records for one kind are sequential from their section
offset; each record therefore starts 16-aligned and its payload (offset +16)
stays 16-aligned, so vertex data can be `memcpy`'d or SSE-walked in place.

```cpp
struct BufferHeader
{
  std::uint32_t format;        // per kind, below
  std::uint32_t strideBytes;   // must match format
  std::uint32_t elementCount;
  std::uint32_t reserved;      // 0
};
static_assert(sizeof(BufferHeader) == 16, "NMO BufferHeader size");

enum class IndexFormat : std::uint32_t { U16 = 0, U32 = 1 };  // stride 2 / 4
enum class VertexFormat : std::uint32_t { Standard = 0 };     // stride 52
enum class SkinFormat : std::uint32_t { Standard = 0 };       // stride 32
```

`IndexFormat::U16` is strongly preferred (half the bandwidth, and universally
optimal on D3D9 hardware); `U32` exists so a single terrain-scale mesh no
longer forces buffer splitting. Writers must use `U16` whenever the referenced
vertex range fits.

```cpp
struct Vertex                        // layout-identical to CMO Vertex
{
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT4 tangent;         // xyz + handedness in w
  std::uint32_t color;               // RGBA bytes (§4.1)
  DirectX::XMFLOAT2 uv;
};
static_assert(sizeof(Vertex) == 52, "NMO Vertex size");

struct SkinVertex                    // layout-identical to CMO SkinningVertex
{
  std::uint32_t boneIndex[BoneInfluences];
  float boneWeight[BoneInfluences];
};
static_assert(sizeof(SkinVertex) == 32, "NMO SkinVertex size");
```

**Pairing rule (explicit where CMO was implicit):** `skinBufferCount` is `0`
or equal to `vertexBufferCount`; skin buffer *i* is the companion of vertex
buffer *i* and its `elementCount` is either `0` (that VB is rigid) or equal to
the VB's. **Influence rule:** weights are non-negative, sum to 1, sorted
descending; unused influences are index 0, weight 0.

### 4.6 Submeshes

The CMO `SubMesh` plus: a complete draw range, its own extents, and the
offsets that carry R-NMO-1 and R-NMO-2.

```cpp
struct MeshExtents                   // layout-identical to CMO MeshExtents
{
  DirectX::XMFLOAT3 center;
  float radius;
  DirectX::XMFLOAT3 boxMin;
  DirectX::XMFLOAT3 boxMax;
};
static_assert(sizeof(MeshExtents) == 40, "NMO MeshExtents size");

struct SubMesh
{
  std::uint32_t materialIndex;
  std::uint32_t indexBufferIndex;
  std::uint32_t vertexBufferIndex;   // companion skin buffer has the same index
  std::uint32_t startIndex;          // first index in the IB
  std::uint32_t primitiveCount;      // triangles
  std::uint32_t baseVertex;          // added to every index at draw time
  std::uint32_t minVertex;           // lowest baseVertex-biased vertex used
  std::uint32_t vertexCount;         // biased vertices lie in [minVertex, minVertex + vertexCount)
  std::uint32_t flags;               // 0 in v1.0
  std::uint32_t boneCount;           // submesh bone table; 0 = bind to mesh skeleton
  std::uint32_t bonesOffset;         // -> bone records            (R-NMO-1)
  std::uint32_t clipCount;
  std::uint32_t clipsOffset;         // -> clip records            (R-NMO-1)
  std::uint32_t markerCount;
  std::uint32_t markersOffset;       // -> marker records          (R-NMO-2)
  MeshExtents extents;               // bind-pose bounds of this submesh
  std::uint32_t reserved[7];         // 0
};
static_assert(sizeof(SubMesh) == 128, "NMO SubMesh size");
```

The three added range fields make the D3D9 call complete and checkable —
`DrawIndexedPrimitive(D3DPT_TRIANGLELIST, baseVertex, minVertex - baseVertex,
vertexCount, startIndex, primitiveCount)` — and give the validator (§4.10) a
closed statement of which vertices a submesh may touch, which the submesh
skeleton rules below depend on. Submesh extents are bind-pose bounds; a
converter animating a part far from its bind pose should inflate them by the
clip's maximum displacement.

### 4.7 Skeletons and bone records

The `Bone` struct is CMO's, unchanged. A **bone record** is: `String` name,
`Bone`, then one NMO extension field, `int32_t meshBoneIndex`. The same record
shape is used at both scopes — one parsing function serves both.

```cpp
struct Bone                          // layout-identical to CMO Bone
{
  std::int32_t parentIndex;          // NoParent for a root; always < own index
  DirectX::XMFLOAT4X4 invBindPose;
  DirectX::XMFLOAT4X4 bindPose;
  DirectX::XMFLOAT4X4 localTransform;
};
static_assert(sizeof(Bone) == 196, "NMO Bone size");
```

- **Mesh scope** (`MeshHeader.boneCount` / `bonesOffset`): the mesh skeleton,
  exactly as CMO. `meshBoneIndex` must be `NoBone`.
- **Submesh scope** (`SubMesh.boneCount` / `bonesOffset`): a submesh-local
  bone table. Per entry, `meshBoneIndex` selects one of two meanings:
  - `NoBone` — a genuinely local bone, owned and animated by this submesh.
  - `>= 0` — an **alias** of mesh bone `meshBoneIndex`: this entry stands for
    that mesh bone in the submesh's index space. Its `Bone` matrices must be
    copies of the mesh bone's (tools duplicate them so a palette can be built
    without touching the mesh table).

`parentIndex` always indexes the table the record sits in. A local bone may
parent onto an aliased entry — that is how a turret's root hangs off a hull
bone. All bone hierarchies, both scopes, evaluate in **mesh space**: a root's
`localTransform` is mesh-relative, so shared vertex buffers stay consistent.
Skinning indices in a `SkinVertex` resolve against the submesh's **bone
scope**: the submesh table when `boneCount > 0`, else the mesh table. §5 gives
the semantics and the reasons.

### 4.8 Animation clips and keyframes

A **clip record** is: `String` name, `Clip`, then `Keyframe[keyCount]` — CMO's
structures, unchanged:

```cpp
struct Clip                          // layout-identical to CMO Clip
{
  float startSeconds;
  float endSeconds;                  // >= startSeconds
  std::uint32_t keyCount;            // 0 allowed: a held pose
};
static_assert(sizeof(Clip) == 12, "NMO Clip size");

struct Keyframe                      // layout-identical to CMO Keyframe
{
  std::uint32_t boneIndex;           // into the clip's bone scope
  float timeSeconds;                 // within [startSeconds, endSeconds]
  DirectX::XMFLOAT4X4 transform;     // replaces the bone's localTransform
};
static_assert(sizeof(Keyframe) == 72, "NMO Keyframe size");
```

Rules NMO adds to CMO's definition:

- **Scope.** Mesh clips index the mesh bone table. Submesh clips index the
  submesh's bone scope — its own table when `boneCount > 0`; when
  `boneCount == 0`, its clips are permitted and index the **mesh** table
  (same rig, private pose — see §5.1 row 2). Clip names must be unique within
  their scope; runtime addressing is `(scope, name)`.
- **Ordering.** Keys sort by `boneIndex`, then strictly increasing
  `timeSeconds`. The flat CMO-shaped array is thereby already a sequence of
  contiguous per-bone tracks: a loader slices it without copying, playback
  keeps a cursor per track, seeks binary-search within a track.
- **Sampling semantics.** Linear interpolation between adjacent keys; clamp at
  track ends; a bone with no keys in a clip holds its `localTransform`. (v1.0
  interpolates matrices because CMO keys are matrices; §8.1 is the planned
  fix.)

### 4.9 Markers (R-NMO-2)

A **marker record** is: `String` name, then `Marker`:

```cpp
struct Marker
{
  DirectX::XMFLOAT3 position;        // mesh space, bind pose
  std::int32_t parentBone;           // into the submesh's bone scope; NoBone = rigid
  std::uint32_t flags;               // 0 in v1.0
};
static_assert(sizeof(Marker) == 20, "NMO Marker size");
```

The requirement is name + `XMFLOAT3`; NMO adds only `parentBone`, because a
marker on an animated submesh is near-useless if it cannot follow the
animation (a muzzle point must ride the recoiling barrel). Semantics, naming
and lookup practice are §6. Marker names must be unique within their submesh.

### 4.10 Validation requirements

A conforming loader **must** verify, in order, before using any data — and
reject the file on any failure (never repair):

1. `magic == FileMagic`; `versionMajor == 1`; `headerBytes == 32`;
   `fileBytes` equals the actual size read.
2. Every `MeshRef` window lies within `[headerBytes + meshCount * 8,
   fileBytes)`; all arithmetic in 64-bit before any allocation or pointer
   math (counts and offsets are attacker-controlled until proven otherwise —
   the survey in [AssetPipeline.md](AssetPipeline.md) Appendix B records what
   trusting loaders did to this codebase).
3. Every offset in `MeshHeader`/`SubMesh` plus its section's computed size
   lies within the mesh blob; alignment as specified; every `String` obeys
   `MaxStringBytes` and valid UTF-8.
4. `BufferHeader.strideBytes` matches its declared format; skin/VB pairing
   and element-count rules of §4.5 hold.
5. Per submesh: `materialIndex`, `indexBufferIndex`, `vertexBufferIndex` in
   range; the index range `[startIndex, startIndex + 3 * primitiveCount)`
   fits its IB; `minVertex >= baseVertex`; every baseVertex-biased index
   falls in `[minVertex, minVertex + vertexCount)` and within the VB.
6. Per bone table: `parentIndex < ownIndex` (this makes cycles impossible and
   pose evaluation a single forward loop); submesh aliases satisfy
   `meshBoneIndex < mesh boneCount`; mesh-scope records carry `NoBone`.
7. Per clip: times ordered and within range; key ordering of §4.8; every
   `boneIndex` within the clip's bone scope.
8. Per skinned submesh (its VB has a non-empty companion): its bone scope is
   non-empty; every `SkinVertex.boneIndex` of vertices in
   `[minVertex, minVertex + vertexCount)` is within the scope; two skinned
   submeshes with **different** bone scopes must not have overlapping vertex
   ranges (same scope may share freely).
9. Per marker: `parentBone` is `NoBone` or within the submesh's bone scope.
10. Unknown **flag** bits: ignore (§4.11 guarantees they are ignorable).
    Nonzero **reserved** fields: ignore likewise. `versionMinor` greater than
    the loader knows: load, optionally warn.

Sanity caps are recommended (not normative): `meshCount ≤ 4096`,
`boneCount ≤ 1024` per scope, `materialCount ≤ 256` — they bound what a
hostile or corrupt file can make the loader allocate before deeper checks
run. Debug builds report failures through `DEBUG_ASSERT_TEXT` with the field
and value (R9); release builds fail the load cleanly.

### 4.11 Versioning policy

- **Minor bump (additive only).** New data may arrive only in ways v1.0
  loaders already ignore: a reserved field in `FileHeader`/`MeshHeader`/
  `SubMesh` becomes a new count/offset pair to a new section, or a flag bit
  is defined whose meaning is *safe to ignore*. Existing sections, strides
  and record framings never change.
- **Major bump (breaking).** Anything else — including extending an existing
  record's framing. This is why the marker orientation extension (§8.5) is
  specified as a *new* section reached through `SubMesh.reserved`, not as a
  conditional tail on the v1.0 marker record.

### 4.12 Recommended physical order

Offsets are authoritative, but the reference writer emits each mesh blob in
the §3.2 order — metadata first (header, name, materials, submesh table and
its bone/clip/marker records), bulk buffers after, mesh skeleton and clips
last. Rationale: everything a loader parses field-by-field sits in the first
few KB (one warm read), and the bulk payload that is consumed by `memcpy` to
GPU buffers sits contiguously behind it. `MeshRef` also allows loading one
mesh of a many-mesh file with a single seek.

## 5. Submesh bone animation — semantics and best practice (R-NMO-1)

### 5.1 The four configurations

| `boneCount` | `clipCount` | Meaning | Typical use |
|---|---|---|---|
| 0 | 0 | Plain CMO submesh: rigid, or skinned against the mesh skeleton, driven by mesh clips | body panels, terrain props |
| 0 | > 0 | Clips scoped to this submesh but indexing the **mesh** skeleton — same rig, private pose instance | a variant idle on one part without duplicating the rig |
| > 0, all aliased | 0 | A **bone palette**: the submesh names which mesh bones it uses; mesh clips drive them | splitting big rigs under the shader-constant ceiling (§5.2) |
| > 0, local or mixed | ≥ 0 | An independent or semi-independent articulated part with its own CMO-style skeleton and clips | turret on a hull, radar dish, Derrick pump arm |

The last row is the requirement itself: the structures inside it — named
bones, named clips, keyframes — are byte-for-byte the CMO definitions.

### 5.2 Why aliasing is in the format and not left to runtimes

Two forces make per-submesh bone tables necessary rather than decorative:

- **The D3D9 constant ceiling.** vs_2_0/vs_3_0 guarantee 256 float4 vertex
  shader constants. A skinning palette stores one 4×3 matrix (3 registers)
  per bone; reserving ~16 registers for the WVP matrix, lighting and fog
  leaves ⌊240 / 3⌋ = **80 bones per draw call**. Any rig beyond that must be
  drawn as multiple submeshes, each carrying only the bones it references —
  which is exactly a submesh bone table whose entries alias mesh bones. With
  aliasing in the file, the converter computes the split once, offline;
  without it, every loader rediscovers per-submesh bone usage by scanning
  skin vertices at load.
- **Independent parts must not be welded to the body rig.** A turret that
  traverses while the hull plays a walk cycle is two animation timelines. In
  CMO, both would live in one mesh-level clip set and the runtime would have
  to mask bones per part by name convention. NMO states the partition in
  data: hull bones in the mesh skeleton, turret bones local to the turret
  submesh, the turret root parented onto an aliased hull bone.

Note the engine's *current* pipeline transforms on the CPU
([Phase8Plan.md](Phase8Plan.md), [Phase10Plan.md](Phase10Plan.md) keep the
`D3DFVF_XYZRHW` funnel), where no constant ceiling exists — but a CPU skinner
still wins from local tables (smaller matrix arrays, better locality), and the
format must not bake in an assumption the shader path breaks three phases
later.

### 5.3 Pose evaluation and override rules

For one submesh, one frame:

1. Evaluate the mesh pose (mesh clips → mesh bones, forward loop in table
   order; unkeyed bones hold `localTransform`).
2. If the submesh has a bone table: for **aliased** entries take the mesh
   bone's evaluated world (mesh-space) transform; for **local** entries
   evaluate the local chain the same way, resolving a `parentIndex` that
   lands on an aliased entry through the mesh pose. If a submesh clip keys an
   aliased entry, the local key **overrides** the mesh pose for this submesh
   only.
3. Skin matrices: `palette[j] = world[j] × invBindPose[j]`, indices `j` in
   submesh scope. Rigid submeshes skip it all.

What NMO deliberately does **not** define: cross-fade, layering weights, clip
priorities. Those are runtime policy; encoding them in a mesh file is how
formats rot. The file gives cleanly separated scopes; the animation system
composes them.

### 5.4 Redundancy worth knowing about

CMO's `Bone` carries `invBindPose`, `bindPose` **and** `localTransform` —
196 bytes of which one matrix is always derivable (`bindPose` is the walk of
`localTransform` up the hierarchy, and `invBindPose` its inverse). v1.0 keeps
all three for CMO fidelity and tool convenience (64 bytes × bones is noise at
these scales — a 60-bone rig spends 3.7 KB extra). If bones ever number in the
thousands per file, dropping `bindPose` is the first 21 % saving available,
behind a minor version. Not worth breaking symmetry with CMO now.

## 6. Markers — semantics and best practice (R-NMO-2)

**Spaces.** `position` is stored in mesh space at bind pose — the same space
as vertices, so artists place markers in the modeling tool and the converter
copies coordinates through. A bound marker (`parentBone >= 0`) follows its
bone with **the same transform skinning uses**:
`world = boneWorld × invBindPose × position` — one rule shared with §5.3, no
second convention to implement or get wrong. A rigid marker is
`world = meshWorld × position`.

**Why `parentBone` and not the bare minimum.** The requirement's minimum
(name + position) describes a static point. On any animated submesh the
useful point rides the skeleton: muzzle on a recoiling barrel, hatch hinge on
a turret. One `int32` per marker (markers are counted in ones and tens)
purchases that; `NoBone` remains the requirement's plain static point.

**What is deliberately missing: orientation.** A muzzle needs a direction,
not only a position. It is *not* in v1.0 because the stated requirement is
position-only and because appending fields to the marker record would break
the record framing (§4.11). The v1.1 path is §8.5. Until then the engine's
existing convention applies (connectors are positions; directions come from
the part's transform), so nothing regresses.

**Naming.** Names are the contract between art and code — validate them like
one. Recommended conventions: `Muzzle0`…`MuzzleN`, `TurretMount`, `Exhaust0`,
`HitFlash`; converter-generated markers from legacy `.pie` connectors are
`Connector00`, `Connector01`, … in file order so existing index-based game
code has a mechanical migration (connector *i* → marker `Connector0i`),
after which call sites migrate to real names at leisure.

**Lookup practice.** Never string-compare per frame. At load, hash each name
(FNV-1a 32-bit) into a per-submesh sorted array; lookups are a binary search
over a handful of entries; detect hash collisions at load and fail loudly
(the tool renames — a 32-bit collision inside one submesh's marker list is
lottery-rare and must be impossible to ship). Do **not** store hashes in the
file: derivable data in a file is a consistency liability (hash function
changes strand shipped assets), and hashing at load costs microseconds.

## 7. Loading and runtime best practice

- **One read, validate, use in place.** Read the whole file (or one
  `MeshRef` window) into a single 16-aligned allocation, run §4.10, then
  point runtime structures into the blob: buffer payloads feed
  `IDirect3DDevice9::CreateVertexBuffer` / `CreateIndexBuffer` +
  `memcpy` (managed pool, per the device-loss conventions
  [Screen.cpp](../NeuronClient/Screen.cpp) owns); keyframe arrays are used
  directly as tracks thanks to the §4.8 ordering. The legacy tree already
  learned this lesson — `IMD_BINARY` ("treat as one big chunk",
  [IMD.h](../NeuronClient/IMD.h)) is this pattern, a generation early. All
  on-disk structs are trivially copyable; a loader may
  `static_assert(std::is_trivially_copyable_v<T>)` on each as insurance.
- **Parse the named records once** (materials, bones, clips, markers) into
  compact runtime tables with hashed names; the blob's string bytes are not
  referenced after load.
- **Prefer merged buffers.** Converters should emit one VB/IB per mesh where
  vertex ranges allow, with submeshes as ranges (that is what the
  `baseVertex`/`minVertex` fields are for) — fewer stream switches, fewer
  device objects. CMO exporters historically emitted per-material buffers;
  that shape is legal NMO but not the recommended one.
- **Draw order.** Sort submesh draws by material, then buffer pair — the
  usual state-change minimization; the format keeps submeshes independent
  precisely so the renderer may reorder them.
- **Testing.** `NeuronClientTest` gets: one golden hand-authored `.nmo`
  (bytes in the test, so no binary asset lands in `GameData/` — that
  directory's binaries are authored outside this repo per
  [AGENTS.md](../AGENTS.md) §2) exercising every feature — two submeshes, one
  aliased palette, one local turret skeleton, markers bound and rigid; plus
  adversarial loads: truncated at every section boundary, oversize counts
  (the 64-bit-math rule), overlapping skinned ranges with different scopes,
  out-of-range bone/parent/marker indices, bad UTF-8, `parentIndex >=
  ownIndex`, unsorted keyframes. Every §4.10 clause has a test that violates
  exactly it.

## 8. Optimization roadmap — measured reasons, deferred costs

Each entry states the win and why it is not in v1.0. Ordering is by expected
payoff.

### 8.1 SRT keyframes (the big one) — planned v1.1

CMO's 72-byte matrix keyframe is the format's worst inheritance: 64 of the
72 bytes encode 9-ish meaningful DOF, and lerping matrices shears under
rotation. Replacement track section (via a `SubMesh`/`MeshHeader` reserved
offset): per bone, separately keyed translation (`XMFLOAT3`), rotation
(quaternion `XMFLOAT4`, nlerp/slerp at runtime), uniform scale (`float`).

Worked size, 60-bone rig, 3 s clip sampled at 30 Hz (90 samples):

| Encoding | Bytes |
|---|---|
| v1.0 CMO keyframes: 60 × 90 × 72 | 388,800 (~380 KB) |
| SRT keys, uniform scale: 60 × 90 × 36 | 194,400 (~190 KB) |
| + constant-track elimination (half the tracks static, 1 key) | ~98,000 (~96 KB) |

~4× smaller **and** rotationally correct interpolation, which matrix keys
cannot give at any size. Deferred only because R-NMO-1 asks for CMO's
definitions and the container had to come first; the reserved-offset
mechanism exists exactly so this lands without a major bump (old loaders keep
reading the matrix section; tools emit both during the transition, then drop
matrices).

### 8.2 Packed skin vertices — v1.1, with caps check

`SkinVertex` spends 16 bytes on four indices that are ≤ 80 (§5.2) and 16 on
weights. `D3DDECLTYPE_UBYTE4` indices + `D3DDECLTYPE_UBYTE4N` weights: 8
bytes, −75 %. Requires a `SkinFormat::Packed` enum value only — the
`BufferHeader` already carries format and stride. Deferred: the D3D9 caps
bits (`DeclTypes`) must be probed, and the CPU-skinning path (§5.2) prefers
the fat floats; ship when the shader path exists.

### 8.3 Packed vertex attributes — v1.x, measure first

Normal/tangent as `DEC3N`/`UBYTE4N` and half-float UVs take `Vertex` from 52
to ~32 bytes (−38 %). Same caps caveat, plus real visual QA on tangent
precision. Only worth doing once model vertex counts actually pressure
memory or bandwidth — measure before spending (this repo's rule: figures are
measured, not estimated).

### 8.4 Quantized animation — v1.x, after 8.1

Times as `uint16` normalized to the clip, rotations as smallest-three 48-bit
quaternions: a further ~2–3× on top of §8.1. Complexity is real (error
metrics per track, converter tuning); justified only if animation memory
shows up in a profile.

### 8.5 Marker orientation — v1.1, when a consumer exists

New per-submesh section `markers2Offset` (reserved-field mechanism): record =
`String` name + position + `parentBone` + quaternion orientation + uniform
scale. Supersedes the v1.0 marker list per submesh when present (a submesh
uses one list or the other, never both). Specified now so the first consumer
(muzzle direction is the obvious one) does not improvise; not shipped in
v1.0 because the requirement is position-only and empty speculation in a
v1.0 costs forever.

### 8.6 Considered and rejected

- **A string table with indices.** Dedup would save a few hundred bytes on
  files measured in hundreds of KB, at the price of every reader doing two
  hops for every name. Rejected: not worth one indirection.
- **Whole-file compression.** R14 forbids new dependencies, so zlib is out;
  the Windows Compression API (`cabinet.dll`, Win8+) would be sanctionable —
  but model payloads are a rounding error next to `GameData/`'s 520 MB of
  media, and DDS textures dominate model memory anyway. Rejected for v1.0;
  revisit only if NMO ever carries baked animation libraries at scale.
- **A full RIFF/IFF chunk tree.** The offset-table header already provides
  skipping, absence and additive evolution with strictly less machinery; a
  second framing mechanism is one too many. (This is also why §4.11 is so
  short — one rule, "reserved becomes new sections", does all of it.)
- **Name hashes in the file.** §6 — derivable data drifts; compute at load.
- **Curve tracks (Hermite/Bézier).** Converters bake; runtimes stay dumb and
  fast; §8.1/§8.4 recover the memory that baking costs. Revisit never,
  probably.

## 9. Adoption path (when a phase picks this up)

| Piece | Where | Notes |
|---|---|---|
| Format header | `NeuronClient/Nmo.h` | the §4 structs verbatim, `namespace Neuron::Nmo`; no logic |
| Loader | `NeuronClient/NmoLoad.cpp` | §4.10 validation + §7 in-place load; new files go in the `.vcxproj` **and** `.filters` (AGENTS §6) |
| Converter | `tools/convert_pie.py` | `.pie` → `.nmo`: points/polys → welded VB/IB; `texpage` → material with `texture[0]`; connectors → `Connector0N` markers (§6); each PIE level → one mesh |
| `.ani` mapping | same converter, later stage | `ANIM_3D_TRANS` per-sub-object pos/rot/scale states are natively submesh clips (they are SRT keys already — they should land straight into §8.1 tracks, skipping matrix keyframes entirely) |
| Validation gate | `tools/validate_assets.py` | structural check of shipped `.nmo` once any exist, same green-gate rule as the JSON manifests |

Two legacy features do **not** map and must be resolved by the adopting
phase, not silently dropped: per-polygon frame-based texture animation
(`iTexAnim`, heavily used — 1,693 polygons) has no NMO representation
(candidates: material-UV clip tracks as a v1.1 section, or keep it
engine-side per material); and `ANIM_3D_FRAMES` whole-shape-per-frame
animation is mesh *swapping*, not skeletal animation — it likely stays an
engine behavior selecting among meshes, which NMO's multi-mesh files
represent naturally.

## 10. Open questions

1. **Texture/UV animation** — v1.1 material tracks, or engine-side? Decide
   when the converter meets the first `iTexAnim` polygon (§9).
2. **Marker orientation** — is any v1.0 consumer blocked without it? If a
   launch feature needs muzzle directions, promote §8.5 into v1.0 before the
   first shipped file rather than after (pre-ship is the only cheap moment
   to change a record framing).
3. **Blend policy location** — §5.3 pushes cross-fade/layering to the
   runtime; the animation system that replaces `Anim.cpp` should get its own
   short design note when built.
4. **Coexistence** — NMO does not replace `.pie` by decree; per
   [AssetPipeline.md](AssetPipeline.md), formats are replaced only when a
   phase owns the conversion end-to-end, converter and loader and data in
   one motion. Until then both formats coexist behind the resource system.

---

## Appendix A — stream grammar, in CMO.h's own comment style

Offsets are authoritative (§4.3); this shows the recommended writer layout
(§4.12). `String` = `uint32` byte length + UTF-8 + pad to 4.

```
// .NMO files
//
// FileHeader (32 bytes: magic 'NMO1', version, headerBytes, fileBytes,
//             meshCount, flags, payloadCrc32, reserved)
// MeshRef[meshCount] (8 bytes each: offsetBytes, lengthBytes)
// { [meshCount]  - each blob at MeshRef.offsetBytes, 16-byte aligned
//      MeshHeader (96 bytes - counts and blob-relative offsets of all sections)
//      String - name of mesh
//      { [materialCount]
//          String - name of material
//          Material structure
//          String - name of pixel shader
//          { [8]
//              String - name of texture
//          }
//      }
//      SubMesh[subMeshCount] (128 bytes each - draw range, extents, and
//                             offsets/counts of its bones, clips, markers)
//      { [per submesh, at the offsets its SubMesh record names]
//          { [subMesh.boneCount]                            - R-NMO-1
//              String - name of bone
//              Bone structure
//              INT - meshBoneIndex (-1 local, else alias of mesh bone)
//          }
//          { [subMesh.clipCount]                            - R-NMO-1
//              String - name of clip
//              Clip structure
//              { [keyCount]
//                  Keyframe structure   - sorted (boneIndex, timeSeconds)
//              }
//          }
//          { [subMesh.markerCount]                          - R-NMO-2
//              String - name of marker
//              Marker structure (XMFLOAT3 position, INT parentBone, UINT flags)
//          }
//      }
//      { [indexBufferCount]
//          BufferHeader (16 bytes) - USHORT[] or UINT[] indices - pad to 16
//      }
//      { [vertexBufferCount]
//          BufferHeader (16 bytes) - Vertex[] - pad to 16
//      }
//      { [skinBufferCount]  - 0 or vertexBufferCount; pairs by index
//          BufferHeader (16 bytes) - SkinVertex[] - pad to 16
//      }
//      MeshExtents structure
//      { [boneCount]   - mesh skeleton; meshBoneIndex always -1
//          String - name of bone
//          Bone structure
//          INT - meshBoneIndex
//      }
//      { [clipCount]   - mesh clips, as CMO
//          String - name of clip
//          Clip structure
//          { [keyCount]
//              Keyframe structure
//          }
//      }
// }
```
