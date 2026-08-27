#pragma once

/*
 * NmoLoad.h
 *
 * Reading a .nmo file: validate it, then use it where it lies.
 *
 * The whole file is read into one allocation the Model owns, checked against
 * the rules in Docs/NeuronMeshObject.md, and then described by views that
 * point into that allocation. Nothing is copied out of it - the bulk arrays
 * are handed to the device as they are, and a name is a string_view over the
 * file's own bytes. The legacy loader learned this the hard way: IMD_BINARY
 * ("treat as one big chunk") is the same idea a generation earlier.
 *
 * A malformed file is rejected, never repaired. Load returns false, says why
 * through LoadError, and leaves the Model empty; it does not assert, because
 * a bad asset must not take a debug build down with it.
 */

#include <cstdint>
#include <string_view>
#include <vector>

#include "Nmo.h"

namespace Neuron::Nmo
{

enum class LoadError : std::uint32_t
{
  None = 0,
  NotNmo, // the magic is wrong - this is not an NMO file at all
  UnsupportedVersion, // a major version this build does not know
  Truncated, // a record or section runs past the end of its blob
  BadChecksum, // payloadCrc32 disagrees with the payload
  BadOffset, // an offset is zero, misaligned, or outside its blob
  BadString, // over MaxStringBytes, or not valid UTF-8
  BadCount, // a count exceeds the sanity caps in Nmo.h
  BadBuffer, // a stride that does not match its format, or a skin/vertex mismatch
  BadDrawRange, // a submesh addresses indices or vertices it does not own
  BadBone, // a parent that is not an earlier bone, or an alias of a bone that is not there
  BadClip, // an unknown encoding, unsorted keys, or a bone outside the clip's scope
  BadMarker, // a duplicate name, or a bone outside the submesh's scope
  BadSkin, // a skin index outside the submesh's bone scope
};

/* A short, stable description of a LoadError, for traces and test failures. */
[[nodiscard]] const char* Describe(LoadError _error);

struct MaterialView
{
  std::string_view name;
  std::string_view shader;
  std::string_view textures[TextureSlots];
  const Material* values = nullptr;
  const MaterialExt* extension = nullptr;
};

struct BoneView
{
  std::string_view name;
  const Bone* values = nullptr;
  std::int32_t meshBoneIndex = NoBone; // >= 0 means this entry aliases a mesh bone
};

/* One animated bone of an SrtTracks clip. The three arrays are contiguous in
 * the file and in this order, so playback walks them with a cursor each. */
struct TrackView
{
  const SrtTrack* values = nullptr;
  const TranslationKey* translation = nullptr;
  const RotationKey* rotation = nullptr;
  const ScaleKey* scale = nullptr;
};

struct ClipView
{
  std::string_view name;
  const Clip* values = nullptr;
  KeyEncoding encoding = KeyEncoding::SrtTracks;
  const Keyframe* keyframes = nullptr; // MatrixKeys only
  std::vector<TrackView> tracks; // SrtTracks only
};

struct MarkerView
{
  std::string_view name;
  const Marker* values = nullptr;
};

struct BufferView
{
  std::uint32_t format = 0;
  std::uint32_t strideBytes = 0;
  std::uint32_t elementCount = 0;
  const void* data = nullptr; // strideBytes * elementCount bytes, 16-byte aligned
};

struct SubMeshView
{
  std::string_view name;
  const SubMesh* values = nullptr;
  std::vector<BoneView> bones;
  std::vector<ClipView> clips;
  std::vector<MarkerView> markers;
  const std::uint32_t* facets = nullptr; // primitiveCount source-polygon ids, or null
};

struct MeshView
{
  std::string_view name;
  const MeshHeader* header = nullptr;
  const MeshExtents* extents = nullptr;
  std::vector<MaterialView> materials;
  std::vector<BufferView> indexBuffers;
  std::vector<BufferView> vertexBuffers;
  std::vector<BufferView> skinBuffers; // empty, or one per vertex buffer
  std::vector<BoneView> bones;
  std::vector<ClipView> clips;
  std::vector<SubMeshView> subMeshes;
};

/* A loaded file. Owns the bytes; the views above borrow from them, so a view
 * outlives nothing. Moving a Model keeps every view valid - the bytes do not
 * move with it - but copying one is forbidden, because two owners of the same
 * views is a mistake waiting to be made. */
class Model
{
public:
  Model() = default;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;
  Model(Model&&) = default;
  Model& operator=(Model&&) = default;

  /* Take ownership of a whole file and validate it. On failure _outModel is
   * left empty and _outError says which rule the file broke. */
  [[nodiscard]] static bool Load(std::vector<std::uint8_t> _bytes, Model& _outModel, LoadError& _outError, bool _verifyChecksum = true);

  [[nodiscard]] std::size_t MeshCount() const { return m_meshes.size(); }
  [[nodiscard]] const MeshView& Mesh(std::size_t _index) const { return m_meshes[_index]; }
  [[nodiscard]] const std::vector<MeshView>& Meshes() const { return m_meshes; }
  [[nodiscard]] std::size_t SizeBytes() const { return m_bytes.size(); }

private:
  std::vector<std::uint8_t> m_bytes;
  std::vector<MeshView> m_meshes;
};

/* CRC-32 (reflected, polynomial 0xEDB88320) over a byte range - the check the
 * file header records. Exposed because the writer needs the same function. */
[[nodiscard]] std::uint32_t Crc32(const std::uint8_t* _data, std::size_t _bytes);

} // namespace Neuron::Nmo
