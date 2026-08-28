#pragma once

/*
 * Nmo.h
 *
 * The Neuron Mesh Object file layout: the structures a .nmo file is made of,
 * and nothing else. No logic lives here; NmoLoad.h is the loader.
 *
 * The format is specified in Docs/NeuronMeshObject.md and this header is its
 * C++ half. tools/blender_nmo/nmo_format.py is the other half - the reference
 * codec the tools and tests are built on - so the two must agree field for
 * field. The static_asserts below are that agreement, checked at compile time
 * here and at import time there.
 *
 * Everything is little-endian, naturally aligned and free of packing pragmas:
 * the layouts are designed so the default MSVC and GCC packing produce the
 * documented size, which is what lets a loaded file be used in place instead
 * of copied field by field. Nothing here is size-dependent - no wchar_t, no
 * pointers, no long - so a file written by a 32 bit build is byte-identical
 * to one written by a 64 bit build (Docs/X64Readiness.md).
 */

#include <cstdint>
#include <type_traits>

#include <directxmath.h>

namespace Neuron::Nmo
{

inline constexpr std::uint32_t FileMagic = 0x314F4D4E; // "NMO1" on disk
inline constexpr std::uint16_t VersionMajor = 1;
inline constexpr std::uint16_t VersionMinor = 0;
inline constexpr std::uint32_t MaxStringBytes = 1024;
inline constexpr std::uint32_t TextureSlots = 8; // as CMO
inline constexpr std::uint32_t BoneInfluences = 4; // as CMO
inline constexpr std::int32_t NoParent = -1;
inline constexpr std::int32_t NoBone = -1;

/* Section alignment. Buffers are 16-byte aligned so their payload can be fed
 * to a vertex buffer or walked with SIMD without a copy; record streams need
 * only the 4 bytes their fields want. */
inline constexpr std::uint32_t BufferAlignment = 16;
inline constexpr std::uint32_t RecordAlignment = 4;

/* Sanity caps. Not part of the format - a conforming file may exceed them -
 * but a loader applies them before allocating, so a corrupt count cannot ask
 * for a gigabyte before the deeper checks run. */
inline constexpr std::uint32_t MaxMeshCount = 4096;
inline constexpr std::uint32_t MaxBoneCount = 1024;
inline constexpr std::uint32_t MaxMaterialCount = 256;

struct FileHeader
{
  std::uint32_t magic; // FileMagic
  std::uint16_t versionMajor; // breaking changes only
  std::uint16_t versionMinor; // additive changes only
  std::uint32_t headerBytes; // sizeof(FileHeader)
  std::uint32_t fileBytes; // total file size, for validation
  std::uint32_t meshCount;
  std::uint32_t flags; // 0 in v1.0
  std::uint32_t payloadCrc32; // CRC-32 of [headerBytes, fileBytes); 0 = not computed
  std::uint32_t reserved;
};

struct MeshRef
{
  std::uint32_t offsetBytes; // from file start; 16-byte aligned
  std::uint32_t lengthBytes; // whole mesh blob
};

/* Every offset in MeshHeader and SubMesh is relative to the first byte of the
 * mesh blob, never to the file. 0 means the section is absent. */
struct MeshHeader
{
  std::uint32_t flags; // 0 in v1.0
  std::uint32_t nameOffset; // -> String; 0 = unnamed
  std::uint32_t materialCount;
  std::uint32_t materialsOffset; // -> material records, sequential
  std::uint32_t subMeshCount;
  std::uint32_t subMeshesOffset; // -> SubMesh[subMeshCount]
  std::uint32_t indexBufferCount;
  std::uint32_t indexBuffersOffset; // -> buffer records, sequential
  std::uint32_t vertexBufferCount;
  std::uint32_t vertexBuffersOffset;
  std::uint32_t skinBufferCount; // 0, or == vertexBufferCount
  std::uint32_t skinBuffersOffset;
  std::uint32_t extentsOffset; // -> MeshExtents
  std::uint32_t boneCount; // mesh skeleton; 0 = none, so no flag byte is needed
  std::uint32_t bonesOffset; // -> bone records, sequential
  std::uint32_t clipCount; // mesh clips
  std::uint32_t clipsOffset; // -> clip records, sequential
  std::uint32_t reserved[7]; // 0; a future minor version claims these
};

/* Layout-identical to the CMO Material, so a CMO exporter's output drops
 * straight in. This engine fills texture[0] and leaves the Phong fields at
 * their defaults; they are kept for CMO compatibility and for a shader path
 * that does not exist yet. */
struct Material
{
  DirectX::XMFLOAT4 ambient;
  DirectX::XMFLOAT4 diffuse;
  DirectX::XMFLOAT4 specular;
  float specularPower;
  DirectX::XMFLOAT4 emissive;
  DirectX::XMFLOAT4X4 uvTransform;
};

enum class RenderFlags : std::uint32_t
{
  None = 0,
  DoubleSided = 0x1, // do not backface-cull
  AlphaTest = 0x2, // the colour-keyed texel is transparent
  Additive = 0x4,
};

/* What selects the atlas tile. The distinction matters because the .pie
 * renderer overloaded one parameter with both meanings: 1,543 of the 1,693
 * texture-animated polygons in the shipped models declare eight frames
 * because eight is the player count, not because anything animates. */
enum class AtlasSelector : std::uint32_t
{
  None = 0,
  Time = 1, // the frame advances every atlasFrameMs
  Team = 2, // the frame is the owning player's index
};

struct MaterialExt
{
  std::uint32_t renderFlags; // RenderFlags bitmask
  std::uint32_t atlasFrameCount; // 0 = the texture is not an atlas
  std::uint32_t atlasTileWidthTexels;
  std::uint32_t atlasTileHeightTexels;
  std::uint32_t atlasFramesPerRow; // 0 = derive from page width / tile width
  std::uint32_t atlasSelector; // AtlasSelector
  std::uint32_t atlasFrameMs; // Time only; 0 otherwise
  std::uint32_t reserved;
};

enum class IndexFormat : std::uint32_t
{
  U16 = 0, // stride 2 - preferred, and sufficient for every shipped model
  U32 = 1, // stride 4
};

enum class VertexFormat : std::uint32_t
{
  Standard = 0, // stride 52
};

enum class SkinFormat : std::uint32_t
{
  Standard = 0, // stride 32
};

struct BufferHeader
{
  std::uint32_t format; // IndexFormat, VertexFormat or SkinFormat, per section
  std::uint32_t strideBytes; // must match format
  std::uint32_t elementCount;
  std::uint32_t reserved;
};

/* Layout-identical to the CMO Vertex. Colour is R,G,B,A at ascending
 * addresses; the D3D9 diffuse path wants D3DCOLOR, which is B,G,R,A, so the
 * loader swizzles on upload rather than the file carrying a device format. */
struct Vertex
{
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT4 tangent; // xyz + handedness in w
  std::uint32_t color;
  DirectX::XMFLOAT2 uv;
};

/* Layout-identical to the CMO SkinningVertex. Weights are non-negative, sum
 * to 1 and are sorted descending; an unused influence is index 0, weight 0. */
struct SkinVertex
{
  std::uint32_t boneIndex[BoneInfluences];
  float boneWeight[BoneInfluences];
};

struct MeshExtents
{
  DirectX::XMFLOAT3 center;
  float radius;
  DirectX::XMFLOAT3 boxMin;
  DirectX::XMFLOAT3 boxMax;
};

enum class SubMeshFlags : std::uint32_t
{
  None = 0,
  /* The game rewrites these vertices every frame - conforming a base plate to
   * the terrain under it, or jittering a structure hit by electronic damage.
   * A loader must keep a writable copy of such a submesh instead of handing
   * it to a static vertex buffer, and the failure is silent if it does not. */
  DeformedAtRuntime = 0x1,
};

struct SubMesh
{
  std::uint32_t materialIndex;
  std::uint32_t indexBufferIndex;
  std::uint32_t vertexBufferIndex; // its companion skin buffer has the same index
  std::uint32_t startIndex; // first index in the index buffer
  std::uint32_t primitiveCount; // triangles
  std::uint32_t baseVertex; // added to every index at draw time
  std::uint32_t minVertex; // lowest baseVertex-biased vertex used
  std::uint32_t vertexCount; // biased vertices lie in [minVertex, minVertex + vertexCount)
  std::uint32_t flags; // SubMeshFlags
  std::uint32_t nameOffset; // -> String; the role, and what the animation data binds to
  std::uint32_t boneCount; // submesh bone table; 0 = bind to the mesh skeleton
  std::uint32_t bonesOffset; // -> bone records
  std::uint32_t clipCount;
  std::uint32_t clipsOffset; // -> clip records
  std::uint32_t markerCount;
  std::uint32_t markersOffset; // -> marker records
  std::uint32_t facetsOffset; // -> uint32[primitiveCount] source-polygon ids; 0 = absent
  MeshExtents extents; // bind-pose bounds of this submesh alone
  std::uint32_t reserved[5];
};

/* Layout-identical to the CMO Bone. A bone record is a String name, this, and
 * one int32 meshBoneIndex: NoBone for a bone the submesh owns, or the index
 * of the mesh bone this entry stands for. A table of nothing but aliases is a
 * bone palette - it names which mesh bones the submesh uses and animates
 * nothing itself. */
struct Bone
{
  std::int32_t parentIndex; // NoParent for a root; always < its own index
  DirectX::XMFLOAT4X4 invBindPose;
  DirectX::XMFLOAT4X4 bindPose;
  DirectX::XMFLOAT4X4 localTransform;
};

/* Layout-identical to the CMO Clip. */
struct Clip
{
  float startSeconds;
  float endSeconds; // >= startSeconds
  std::uint32_t keyCount; // total keys across the payload; 0 is a held pose
};

enum class KeyEncoding : std::uint32_t
{
  MatrixKeys = 0, // Keyframe[keyCount] - exactly what CMO stores
  SrtTracks = 1, // per-bone scale/rotation/translation tracks - the default
};

struct ClipTracks
{
  std::uint32_t encoding; // KeyEncoding
  std::uint32_t trackCount; // SrtTracks only; 0 for MatrixKeys
};

/* Layout-identical to the CMO Keyframe. Sorted by (boneIndex, timeSeconds),
 * so the flat array is already a run of contiguous per-bone tracks. */
struct Keyframe
{
  std::uint32_t boneIndex; // into the clip's bone scope
  float timeSeconds;
  DirectX::XMFLOAT4X4 transform; // replaces the bone's localTransform
};

/* The SrtTracks payload: SrtTrack[trackCount], then for each track in that
 * order its translation keys, its rotation keys and its scale keys. */
struct SrtTrack
{
  std::uint32_t boneIndex;
  std::uint32_t translationKeyCount;
  std::uint32_t rotationKeyCount;
  std::uint32_t scaleKeyCount;
};

struct TranslationKey
{
  float timeSeconds;
  DirectX::XMFLOAT3 value;
};

struct RotationKey
{
  float timeSeconds;
  DirectX::XMFLOAT4 value; // quaternion
};

struct ScaleKey
{
  float timeSeconds;
  float value; // uniform
};

/* A marker record is a String name and this. position is in mesh space at
 * bind pose, the same space as the vertices; a marker bound to a bone follows
 * it through the transform skinning already uses, so there is no second
 * convention to get wrong. */
struct Marker
{
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT4 orientation; // quaternion; identity is (0, 0, 0, 1)
  float scale; // uniform; 1 unless a consumer uses it
  std::int32_t parentBone; // into the submesh's bone scope; NoBone = rigid
  std::uint32_t flags; // 0 in v1.0
};

/* The sizes are the specification, not a consequence of it. If one of these
 * fires, the header and Docs/NeuronMeshObject.md have diverged - fix the
 * header, never the assert. */
static_assert(sizeof(FileHeader) == 32, "NMO FileHeader size");
static_assert(sizeof(MeshRef) == 8, "NMO MeshRef size");
static_assert(sizeof(MeshHeader) == 96, "NMO MeshHeader size");
static_assert(sizeof(Material) == 132, "NMO Material size");
static_assert(sizeof(MaterialExt) == 32, "NMO MaterialExt size");
static_assert(sizeof(BufferHeader) == 16, "NMO BufferHeader size");
static_assert(sizeof(Vertex) == 52, "NMO Vertex size");
static_assert(sizeof(SkinVertex) == 32, "NMO SkinVertex size");
static_assert(sizeof(MeshExtents) == 40, "NMO MeshExtents size");
static_assert(sizeof(SubMesh) == 128, "NMO SubMesh size");
static_assert(sizeof(Bone) == 196, "NMO Bone size");
static_assert(sizeof(Clip) == 12, "NMO Clip size");
static_assert(sizeof(ClipTracks) == 8, "NMO ClipTracks size");
static_assert(sizeof(Keyframe) == 72, "NMO Keyframe size");
static_assert(sizeof(SrtTrack) == 16, "NMO SrtTrack size");
static_assert(sizeof(TranslationKey) == 16, "NMO TranslationKey size");
static_assert(sizeof(RotationKey) == 20, "NMO RotationKey size");
static_assert(sizeof(ScaleKey) == 8, "NMO ScaleKey size");
static_assert(sizeof(Marker) == 40, "NMO Marker size");

/* Every on-disk struct must be four-byte aligned and trivially copyable, or
 * "read the file and point at it" stops being sound. */
static_assert(alignof(FileHeader) == 4 && std::is_trivially_copyable_v<FileHeader>, "NMO FileHeader layout");
static_assert(alignof(MeshHeader) == 4 && std::is_trivially_copyable_v<MeshHeader>, "NMO MeshHeader layout");
static_assert(alignof(Material) == 4 && std::is_trivially_copyable_v<Material>, "NMO Material layout");
static_assert(alignof(MaterialExt) == 4 && std::is_trivially_copyable_v<MaterialExt>, "NMO MaterialExt layout");
static_assert(alignof(BufferHeader) == 4 && std::is_trivially_copyable_v<BufferHeader>, "NMO BufferHeader layout");
static_assert(alignof(Vertex) == 4 && std::is_trivially_copyable_v<Vertex>, "NMO Vertex layout");
static_assert(alignof(SkinVertex) == 4 && std::is_trivially_copyable_v<SkinVertex>, "NMO SkinVertex layout");
static_assert(alignof(MeshExtents) == 4 && std::is_trivially_copyable_v<MeshExtents>, "NMO MeshExtents layout");
static_assert(alignof(SubMesh) == 4 && std::is_trivially_copyable_v<SubMesh>, "NMO SubMesh layout");
static_assert(alignof(Bone) == 4 && std::is_trivially_copyable_v<Bone>, "NMO Bone layout");
static_assert(alignof(Keyframe) == 4 && std::is_trivially_copyable_v<Keyframe>, "NMO Keyframe layout");
static_assert(alignof(Marker) == 4 && std::is_trivially_copyable_v<Marker>, "NMO Marker layout");

/* The strides a BufferHeader must declare for each format. */
inline constexpr std::uint32_t IndexStrideU16 = 2;
inline constexpr std::uint32_t IndexStrideU32 = 4;
inline constexpr std::uint32_t VertexStride = sizeof(Vertex);
inline constexpr std::uint32_t SkinStride = sizeof(SkinVertex);

} // namespace Neuron::Nmo
