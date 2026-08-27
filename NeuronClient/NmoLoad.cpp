#include "pch.h"

/*
 * NmoLoad.cpp
 *
 * The .nmo reader. It runs the validation list of Docs/NeuronMeshObject.md
 * §4.11 in that order, and every check is here because getting it wrong turns
 * a corrupt file into a pointer into somebody else's memory: counts and
 * offsets in a file are untrusted input until they have been checked against
 * the blob that carries them.
 *
 * Two habits keep that honest. Offset arithmetic happens in std::size_t
 * (64 bit on x64, and never wider than the file on Win32) before anything is
 * dereferenced, so a count near 2^32 cannot wrap into a small number. And a
 * section is bounds-checked once, at the point it is first addressed, rather
 * than at each element - which is why Reader below returns pointers only
 * through Take().
 */

#include <cstring>

#include "Debug.h"
#include "NmoLoad.h"

namespace Neuron::Nmo
{

namespace
{

/* A cursor over one mesh blob. base is the blob's first byte, which is what
 * every offset in a MeshHeader or SubMesh is relative to.
 *
 * TakeArray hands back a pointer into the file's own bytes, which is the whole
 * point of the format - but it rests on two things. Alignment: every on-disk
 * struct is alignof 4 (Nmo.h asserts it), record offsets are checked to be
 * 4-aligned and buffer offsets 16-aligned, and the allocation the bytes live
 * in is at least 8-aligned, so no read is ever misaligned. And aliasing: this
 * is the usual read-a-struct-out-of-a-buffer pattern, which MSVC - the
 * compiler that builds this - does not reorder across. Anything that needs to
 * survive a stricter compiler goes through memcpy, as the file header does. */
class Reader
{
public:
  Reader(const std::uint8_t* _base, std::size_t _lengthBytes) : m_base(_base), m_lengthBytes(_lengthBytes) {}

  [[nodiscard]] bool Seek(std::uint32_t _offset, std::uint32_t _alignment, std::size_t& _outCursor) const
  {
    if (_offset == 0 || _offset >= m_lengthBytes || (_offset % _alignment) != 0)
      return false;
    _outCursor = _offset;
    return true;
  }

  /* Claim _bytes at _cursor and advance it. Returns null if that would run
   * past the blob, which is the only way a caller gets a pointer at all. */
  [[nodiscard]] const std::uint8_t* Take(std::size_t& _cursor, std::size_t _bytes) const
  {
    if (_cursor > m_lengthBytes || _bytes > m_lengthBytes - _cursor)
      return nullptr;
    const std::uint8_t* at = m_base + _cursor;
    _cursor += _bytes;
    return at;
  }

  template <typename T>
  [[nodiscard]] const T* TakeArray(std::size_t& _cursor, std::size_t _count) const
  {
    if (_count != 0 && _count > m_lengthBytes / sizeof(T))
      return nullptr;
    return reinterpret_cast<const T*>(Take(_cursor, sizeof(T) * _count));
  }

  template <typename T>
  [[nodiscard]] const T* TakeOne(std::size_t& _cursor) const
  {
    return TakeArray<T>(_cursor, 1);
  }

  [[nodiscard]] std::size_t LengthBytes() const { return m_lengthBytes; }

private:
  const std::uint8_t* m_base;
  std::size_t m_lengthBytes;
};

std::size_t AlignUp(std::size_t _value, std::size_t _alignment)
{
  return (_value + _alignment - 1) & ~(_alignment - 1);
}

/* Well-formed UTF-8, rejecting the encodings that let the same character be
 * spelled two ways - an overlong form or a surrogate half is how a name that
 * compares unequal here compares equal somewhere else. */
bool IsValidUtf8(const std::uint8_t* _text, std::size_t _bytes)
{
  std::size_t i = 0;
  while (i < _bytes)
  {
    const std::uint8_t lead = _text[i];
    std::size_t length = 0;
    std::uint32_t code = 0;
    if (lead < 0x80)
    {
      i += 1;
      continue;
    }
    if ((lead & 0xE0) == 0xC0)
    {
      length = 2;
      code = lead & 0x1Fu;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
      length = 3;
      code = lead & 0x0Fu;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
      length = 4;
      code = lead & 0x07u;
    }
    else
    {
      return false;
    }
    if (i + length > _bytes)
      return false;
    for (std::size_t k = 1; k < length; ++k)
    {
      const std::uint8_t continuation = _text[i + k];
      if ((continuation & 0xC0) != 0x80)
        return false;
      code = (code << 6) | (continuation & 0x3Fu);
    }
    if (length == 2 && code < 0x80)
      return false;
    if (length == 3 && code < 0x800)
      return false;
    if (length == 4 && code < 0x10000)
      return false;
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
      return false;
    i += length;
  }
  return true;
}

/* A String: uint32 byte length, that many UTF-8 bytes, zero padding to the
 * next 4-byte boundary. Not terminated - which is why the view borrows. */
bool ReadString(const Reader& _reader, std::size_t& _cursor, std::string_view& _outText, LoadError& _outError)
{
  const std::uint32_t* length = _reader.TakeOne<std::uint32_t>(_cursor);
  if (length == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  if (*length > MaxStringBytes)
  {
    _outError = LoadError::BadString;
    return false;
  }
  const std::uint8_t* text = _reader.Take(_cursor, *length);
  if (text == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  if (!IsValidUtf8(text, *length))
  {
    _outError = LoadError::BadString;
    return false;
  }
  _outText = std::string_view(reinterpret_cast<const char*>(text), *length);
  _cursor = AlignUp(_cursor, RecordAlignment);
  return true;
}

bool ReadMaterial(const Reader& _reader, std::size_t& _cursor, MaterialView& _outMaterial, LoadError& _outError)
{
  if (!ReadString(_reader, _cursor, _outMaterial.name, _outError))
    return false;
  _outMaterial.values = _reader.TakeOne<Material>(_cursor);
  _outMaterial.extension = _reader.TakeOne<MaterialExt>(_cursor);
  if (_outMaterial.values == nullptr || _outMaterial.extension == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  if (!ReadString(_reader, _cursor, _outMaterial.shader, _outError))
    return false;
  for (std::uint32_t slot = 0; slot < TextureSlots; ++slot)
  {
    if (!ReadString(_reader, _cursor, _outMaterial.textures[slot], _outError))
      return false;
  }
  return true;
}

/* A bone record, at either scope. _meshBoneCount is 0 at mesh scope, where an
 * alias is not allowed at all: the mesh skeleton has nothing to alias. */
bool ReadBone(const Reader& _reader, std::size_t& _cursor, std::uint32_t _ownIndex, std::uint32_t _meshBoneCount, bool _atMeshScope,
              BoneView& _outBone, LoadError& _outError)
{
  if (!ReadString(_reader, _cursor, _outBone.name, _outError))
    return false;
  _outBone.values = _reader.TakeOne<Bone>(_cursor);
  const std::int32_t* alias = _reader.TakeOne<std::int32_t>(_cursor);
  if (_outBone.values == nullptr || alias == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  _outBone.meshBoneIndex = *alias;

  /* Parents come before children, always. That single rule makes a cycle
   * unrepresentable and turns pose evaluation into one forward loop. */
  const std::int32_t parent = _outBone.values->parentIndex;
  if (parent != NoParent && (parent < 0 || static_cast<std::uint32_t>(parent) >= _ownIndex))
  {
    _outError = LoadError::BadBone;
    return false;
  }
  if (_atMeshScope)
  {
    if (_outBone.meshBoneIndex != NoBone)
    {
      _outError = LoadError::BadBone;
      return false;
    }
  }
  else if (_outBone.meshBoneIndex != NoBone)
  {
    if (_outBone.meshBoneIndex < 0 || static_cast<std::uint32_t>(_outBone.meshBoneIndex) >= _meshBoneCount)
    {
      _outError = LoadError::BadBone;
      return false;
    }
  }
  return true;
}

bool ReadClip(const Reader& _reader, std::size_t& _cursor, std::uint32_t _boneScope, ClipView& _outClip, LoadError& _outError)
{
  if (!ReadString(_reader, _cursor, _outClip.name, _outError))
    return false;
  _outClip.values = _reader.TakeOne<Clip>(_cursor);
  const ClipTracks* framing = _reader.TakeOne<ClipTracks>(_cursor);
  if (_outClip.values == nullptr || framing == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  if (!(_outClip.values->endSeconds >= _outClip.values->startSeconds))
  {
    _outError = LoadError::BadClip;
    return false;
  }
  _outClip.encoding = static_cast<KeyEncoding>(framing->encoding);
  const std::uint32_t keyCount = _outClip.values->keyCount;

  if (_outClip.encoding == KeyEncoding::MatrixKeys)
  {
    if (framing->trackCount != 0)
    {
      _outError = LoadError::BadClip;
      return false;
    }
    _outClip.keyframes = _reader.TakeArray<Keyframe>(_cursor, keyCount);
    if (keyCount != 0 && _outClip.keyframes == nullptr)
    {
      _outError = LoadError::Truncated;
      return false;
    }
    for (std::uint32_t i = 0; i < keyCount; ++i)
    {
      const Keyframe& key = _outClip.keyframes[i];
      if (key.boneIndex >= _boneScope)
      {
        _outError = LoadError::BadClip;
        return false;
      }
      if (i > 0)
      {
        const Keyframe& previous = _outClip.keyframes[i - 1];
        const bool ordered = key.boneIndex > previous.boneIndex
          || (key.boneIndex == previous.boneIndex && key.timeSeconds > previous.timeSeconds);
        if (!ordered)
        {
          _outError = LoadError::BadClip;
          return false;
        }
      }
    }
    return true;
  }

  if (_outClip.encoding != KeyEncoding::SrtTracks)
  {
    /* An unknown encoding cannot be skipped, because its payload has no
     * stated size - which is why a new one is a major version, not a minor. */
    _outError = LoadError::BadClip;
    return false;
  }

  const SrtTrack* tracks = _reader.TakeArray<SrtTrack>(_cursor, framing->trackCount);
  if (framing->trackCount != 0 && tracks == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  std::uint64_t total = 0;
  _outClip.tracks.reserve(framing->trackCount);
  for (std::uint32_t i = 0; i < framing->trackCount; ++i)
  {
    const SrtTrack& track = tracks[i];
    if (track.boneIndex >= _boneScope)
    {
      _outError = LoadError::BadClip;
      return false;
    }
    TrackView view;
    view.values = &track;
    view.translation = _reader.TakeArray<TranslationKey>(_cursor, track.translationKeyCount);
    view.rotation = _reader.TakeArray<RotationKey>(_cursor, track.rotationKeyCount);
    view.scale = _reader.TakeArray<ScaleKey>(_cursor, track.scaleKeyCount);
    const bool present = (track.translationKeyCount == 0 || view.translation != nullptr)
      && (track.rotationKeyCount == 0 || view.rotation != nullptr) && (track.scaleKeyCount == 0 || view.scale != nullptr);
    if (!present)
    {
      _outError = LoadError::Truncated;
      return false;
    }
    for (std::uint32_t k = 1; k < track.translationKeyCount; ++k)
    {
      if (!(view.translation[k].timeSeconds > view.translation[k - 1].timeSeconds))
      {
        _outError = LoadError::BadClip;
        return false;
      }
    }
    for (std::uint32_t k = 1; k < track.rotationKeyCount; ++k)
    {
      if (!(view.rotation[k].timeSeconds > view.rotation[k - 1].timeSeconds))
      {
        _outError = LoadError::BadClip;
        return false;
      }
    }
    for (std::uint32_t k = 1; k < track.scaleKeyCount; ++k)
    {
      if (!(view.scale[k].timeSeconds > view.scale[k - 1].timeSeconds))
      {
        _outError = LoadError::BadClip;
        return false;
      }
    }
    total += std::uint64_t{track.translationKeyCount} + track.rotationKeyCount + track.scaleKeyCount;
    _outClip.tracks.push_back(std::move(view));
  }
  if (total != keyCount)
  {
    _outError = LoadError::BadClip;
    return false;
  }
  return true;
}

bool ReadMarker(const Reader& _reader, std::size_t& _cursor, std::uint32_t _boneScope, MarkerView& _outMarker, LoadError& _outError)
{
  if (!ReadString(_reader, _cursor, _outMarker.name, _outError))
    return false;
  _outMarker.values = _reader.TakeOne<Marker>(_cursor);
  if (_outMarker.values == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  const std::int32_t parent = _outMarker.values->parentBone;
  if (parent != NoBone && (parent < 0 || static_cast<std::uint32_t>(parent) >= _boneScope))
  {
    _outError = LoadError::BadMarker;
    return false;
  }
  return true;
}

enum class BufferKind
{
  Index,
  Vertex,
  Skin,
};

/* The stride the format must declare, or 0 if the format is not one this
 * version knows. Only a known format can be skipped safely, so an unknown one
 * is a rejection rather than something to step over. */
std::uint32_t StrideFor(std::uint32_t _format, BufferKind _kind)
{
  switch (_kind)
  {
  case BufferKind::Index:
    if (_format == static_cast<std::uint32_t>(IndexFormat::U16))
      return IndexStrideU16;
    if (_format == static_cast<std::uint32_t>(IndexFormat::U32))
      return IndexStrideU32;
    return 0;
  case BufferKind::Vertex:
    return _format == static_cast<std::uint32_t>(VertexFormat::Standard) ? VertexStride : 0;
  case BufferKind::Skin:
    return _format == static_cast<std::uint32_t>(SkinFormat::Standard) ? SkinStride : 0;
  }
  return 0;
}

/* Buffer records of one kind are laid out back to back from their section
 * offset, each starting on a 16-byte boundary so its payload does too. */
bool ReadBuffers(const Reader& _reader, std::uint32_t _offset, std::uint32_t _count, BufferKind _kind,
                 std::vector<BufferView>& _outBuffers, LoadError& _outError)
{
  if (_count == 0)
    return true;
  std::size_t cursor = 0;
  if (!_reader.Seek(_offset, BufferAlignment, cursor))
  {
    _outError = LoadError::BadOffset;
    return false;
  }
  _outBuffers.reserve(_count);
  for (std::uint32_t i = 0; i < _count; ++i)
  {
    cursor = AlignUp(cursor, BufferAlignment);
    const BufferHeader* header = _reader.TakeOne<BufferHeader>(cursor);
    if (header == nullptr)
    {
      _outError = LoadError::Truncated;
      return false;
    }
    const std::uint32_t stride = StrideFor(header->format, _kind);
    if (stride == 0 || header->strideBytes != stride)
    {
      _outError = LoadError::BadBuffer;
      return false;
    }
    const std::uint8_t* payload = nullptr;
    if (header->elementCount != 0)
    {
      if (header->elementCount > _reader.LengthBytes() / stride)
      {
        _outError = LoadError::BadCount;
        return false;
      }
      payload = _reader.Take(cursor, static_cast<std::size_t>(stride) * header->elementCount);
      if (payload == nullptr)
      {
        _outError = LoadError::Truncated;
        return false;
      }
    }
    BufferView view;
    view.format = header->format;
    view.strideBytes = stride;
    view.elementCount = header->elementCount;
    view.data = payload;
    _outBuffers.push_back(view);
    cursor = AlignUp(cursor, BufferAlignment);
  }
  return true;
}

/* Every index a submesh draws must land inside the vertex window it declares.
 * That closes the loop the skinning and bone-scope checks below depend on:
 * once the window is known good, checking the skin entries inside it is
 * enough, and nothing outside it can be reached. */
bool ValidateDrawRange(const MeshView& _mesh, const SubMeshView& _subMesh, LoadError& _outError)
{
  const SubMesh& values = *_subMesh.values;
  if (values.materialIndex >= _mesh.materials.size() || values.indexBufferIndex >= _mesh.indexBuffers.size()
    || values.vertexBufferIndex >= _mesh.vertexBuffers.size())
  {
    _outError = LoadError::BadDrawRange;
    return false;
  }
  const BufferView& indices = _mesh.indexBuffers[values.indexBufferIndex];
  const BufferView& vertices = _mesh.vertexBuffers[values.vertexBufferIndex];

  const std::uint64_t last = std::uint64_t{values.startIndex} + std::uint64_t{values.primitiveCount} * 3u;
  if (last > indices.elementCount)
  {
    _outError = LoadError::BadDrawRange;
    return false;
  }
  if (values.minVertex < values.baseVertex)
  {
    _outError = LoadError::BadDrawRange;
    return false;
  }
  const std::uint64_t windowEnd = std::uint64_t{values.minVertex} + values.vertexCount;
  if (windowEnd > vertices.elementCount)
  {
    _outError = LoadError::BadDrawRange;
    return false;
  }
  for (std::uint64_t i = values.startIndex; i < last; ++i)
  {
    const bool narrow = indices.format == static_cast<std::uint32_t>(IndexFormat::U16);
    const std::uint64_t index = narrow ? static_cast<const std::uint16_t*>(indices.data)[i]
                                       : static_cast<const std::uint32_t*>(indices.data)[i];
    const std::uint64_t biased = index + values.baseVertex;
    if (biased < values.minVertex || biased >= windowEnd)
    {
      _outError = LoadError::BadDrawRange;
      return false;
    }
  }
  return true;
}

bool ValidateSkinning(const MeshView& _mesh, const SubMeshView& _subMesh, std::uint32_t _boneScope, LoadError& _outError)
{
  const SubMesh& values = *_subMesh.values;
  if (values.vertexBufferIndex >= _mesh.skinBuffers.size())
    return true;
  const BufferView& skin = _mesh.skinBuffers[values.vertexBufferIndex];
  if (skin.elementCount == 0)
    return true;
  if (_boneScope == 0)
  {
    _outError = LoadError::BadSkin;
    return false;
  }
  const SkinVertex* entries = static_cast<const SkinVertex*>(skin.data);
  const std::uint64_t windowEnd = std::uint64_t{values.minVertex} + values.vertexCount;
  for (std::uint64_t i = values.minVertex; i < windowEnd; ++i)
  {
    const SkinVertex& entry = entries[i];
    for (std::uint32_t influence = 0; influence < BoneInfluences; ++influence)
    {
      if (entry.boneWeight[influence] != 0.0f && entry.boneIndex[influence] >= _boneScope)
      {
        _outError = LoadError::BadSkin;
        return false;
      }
    }
  }
  return true;
}

bool ReadSubMesh(const Reader& _reader, const MeshView& _mesh, const SubMesh& _values, SubMeshView& _outSubMesh, LoadError& _outError)
{
  _outSubMesh.values = &_values;

  if (_values.nameOffset != 0)
  {
    std::size_t cursor = 0;
    if (!_reader.Seek(_values.nameOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    if (!ReadString(_reader, cursor, _outSubMesh.name, _outError))
      return false;
  }

  const std::uint32_t meshBoneCount = static_cast<std::uint32_t>(_mesh.bones.size());
  if (_values.boneCount != 0)
  {
    if (_values.boneCount > MaxBoneCount)
    {
      _outError = LoadError::BadCount;
      return false;
    }
    std::size_t cursor = 0;
    if (!_reader.Seek(_values.bonesOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outSubMesh.bones.reserve(_values.boneCount);
    for (std::uint32_t i = 0; i < _values.boneCount; ++i)
    {
      BoneView bone;
      if (!ReadBone(_reader, cursor, i, meshBoneCount, false, bone, _outError))
        return false;
      _outSubMesh.bones.push_back(bone);
    }
  }

  /* Skinning indices, clip bone indices and marker bindings all resolve
   * against the same scope: the submesh's own table when it has one, the mesh
   * skeleton otherwise. */
  const std::uint32_t boneScope = _outSubMesh.bones.empty() ? meshBoneCount : static_cast<std::uint32_t>(_outSubMesh.bones.size());

  if (_values.clipCount != 0)
  {
    std::size_t cursor = 0;
    if (!_reader.Seek(_values.clipsOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outSubMesh.clips.reserve(_values.clipCount);
    for (std::uint32_t i = 0; i < _values.clipCount; ++i)
    {
      ClipView clip;
      if (!ReadClip(_reader, cursor, boneScope, clip, _outError))
        return false;
      _outSubMesh.clips.push_back(std::move(clip));
    }
  }

  if (_values.markerCount != 0)
  {
    std::size_t cursor = 0;
    if (!_reader.Seek(_values.markersOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outSubMesh.markers.reserve(_values.markerCount);
    for (std::uint32_t i = 0; i < _values.markerCount; ++i)
    {
      MarkerView marker;
      if (!ReadMarker(_reader, cursor, boneScope, marker, _outError))
        return false;
      for (const MarkerView& existing : _outSubMesh.markers)
      {
        if (existing.name == marker.name)
        {
          _outError = LoadError::BadMarker;
          return false;
        }
      }
      _outSubMesh.markers.push_back(marker);
    }
  }

  if (_values.facetsOffset != 0)
  {
    std::size_t cursor = 0;
    if (!_reader.Seek(_values.facetsOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outSubMesh.facets = _reader.TakeArray<std::uint32_t>(cursor, _values.primitiveCount);
    if (_values.primitiveCount != 0 && _outSubMesh.facets == nullptr)
    {
      _outError = LoadError::Truncated;
      return false;
    }
  }

  if (!ValidateDrawRange(_mesh, _outSubMesh, _outError))
    return false;
  return ValidateSkinning(_mesh, _outSubMesh, boneScope, _outError);
}

bool ReadMesh(const std::uint8_t* _base, std::size_t _lengthBytes, MeshView& _outMesh, LoadError& _outError)
{
  const Reader reader(_base, _lengthBytes);
  std::size_t cursor = 0;
  _outMesh.header = reader.TakeOne<MeshHeader>(cursor);
  if (_outMesh.header == nullptr)
  {
    _outError = LoadError::Truncated;
    return false;
  }
  const MeshHeader& header = *_outMesh.header;

  if (header.materialCount > MaxMaterialCount || header.boneCount > MaxBoneCount)
  {
    _outError = LoadError::BadCount;
    return false;
  }

  if (header.nameOffset != 0)
  {
    if (!reader.Seek(header.nameOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    if (!ReadString(reader, cursor, _outMesh.name, _outError))
      return false;
  }

  if (header.materialCount != 0)
  {
    if (!reader.Seek(header.materialsOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outMesh.materials.reserve(header.materialCount);
    for (std::uint32_t i = 0; i < header.materialCount; ++i)
    {
      MaterialView material;
      if (!ReadMaterial(reader, cursor, material, _outError))
        return false;
      _outMesh.materials.push_back(material);
    }
  }

  if (!ReadBuffers(reader, header.indexBuffersOffset, header.indexBufferCount, BufferKind::Index, _outMesh.indexBuffers, _outError))
    return false;
  if (!ReadBuffers(reader, header.vertexBuffersOffset, header.vertexBufferCount, BufferKind::Vertex, _outMesh.vertexBuffers, _outError))
    return false;
  if (header.skinBufferCount != 0 && header.skinBufferCount != header.vertexBufferCount)
  {
    _outError = LoadError::BadBuffer;
    return false;
  }
  if (!ReadBuffers(reader, header.skinBuffersOffset, header.skinBufferCount, BufferKind::Skin, _outMesh.skinBuffers, _outError))
    return false;
  for (std::size_t i = 0; i < _outMesh.skinBuffers.size(); ++i)
  {
    const BufferView& skin = _outMesh.skinBuffers[i];
    if (skin.elementCount != 0 && skin.elementCount != _outMesh.vertexBuffers[i].elementCount)
    {
      _outError = LoadError::BadBuffer;
      return false;
    }
  }

  if (header.extentsOffset != 0)
  {
    if (!reader.Seek(header.extentsOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outMesh.extents = reader.TakeOne<MeshExtents>(cursor);
    if (_outMesh.extents == nullptr)
    {
      _outError = LoadError::Truncated;
      return false;
    }
  }

  if (header.boneCount != 0)
  {
    if (!reader.Seek(header.bonesOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outMesh.bones.reserve(header.boneCount);
    for (std::uint32_t i = 0; i < header.boneCount; ++i)
    {
      BoneView bone;
      if (!ReadBone(reader, cursor, i, 0, true, bone, _outError))
        return false;
      _outMesh.bones.push_back(bone);
    }
  }

  if (header.clipCount != 0)
  {
    if (!reader.Seek(header.clipsOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    _outMesh.clips.reserve(header.clipCount);
    for (std::uint32_t i = 0; i < header.clipCount; ++i)
    {
      ClipView clip;
      if (!ReadClip(reader, cursor, static_cast<std::uint32_t>(_outMesh.bones.size()), clip, _outError))
        return false;
      _outMesh.clips.push_back(std::move(clip));
    }
  }

  if (header.subMeshCount != 0)
  {
    if (!reader.Seek(header.subMeshesOffset, RecordAlignment, cursor))
    {
      _outError = LoadError::BadOffset;
      return false;
    }
    const SubMesh* table = reader.TakeArray<SubMesh>(cursor, header.subMeshCount);
    if (table == nullptr)
    {
      _outError = LoadError::Truncated;
      return false;
    }
    _outMesh.subMeshes.reserve(header.subMeshCount);
    for (std::uint32_t i = 0; i < header.subMeshCount; ++i)
    {
      SubMeshView subMesh;
      if (!ReadSubMesh(reader, _outMesh, table[i], subMesh, _outError))
        return false;
      _outMesh.subMeshes.push_back(std::move(subMesh));
    }
  }
  return true;
}

} // namespace

std::uint32_t Crc32(const std::uint8_t* _data, std::size_t _bytes)
{
  static std::uint32_t table[256];
  static bool built = false;
  if (!built)
  {
    for (std::uint32_t i = 0; i < 256; ++i)
    {
      std::uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit)
        value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      table[i] = value;
    }
    built = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < _bytes; ++i)
    crc = table[(crc ^ _data[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

const char* Describe(LoadError _error)
{
  switch (_error)
  {
  case LoadError::None: return "no error";
  case LoadError::NotNmo: return "not an NMO file";
  case LoadError::UnsupportedVersion: return "unsupported major version";
  case LoadError::Truncated: return "a record runs past the end of its mesh";
  case LoadError::BadChecksum: return "payload checksum mismatch";
  case LoadError::BadOffset: return "an offset is outside its mesh or misaligned";
  case LoadError::BadString: return "a name is oversize or not valid UTF-8";
  case LoadError::BadCount: return "a count exceeds the sanity caps";
  case LoadError::BadBuffer: return "a buffer stride or pairing is wrong";
  case LoadError::BadDrawRange: return "a submesh addresses data it does not own";
  case LoadError::BadBone: return "a bone parent or alias is invalid";
  case LoadError::BadClip: return "a clip is unsorted, mis-sized or out of scope";
  case LoadError::BadMarker: return "a marker name is duplicated or its bone is out of scope";
  case LoadError::BadSkin: return "a skin index is outside its bone scope";
  }
  return "unknown error";
}

bool Model::Load(std::vector<std::uint8_t> _bytes, Model& _outModel, LoadError& _outError, bool _verifyChecksum)
{
  _outModel.m_bytes.clear();
  _outModel.m_meshes.clear();
  _outError = LoadError::None;

  if (_bytes.size() < sizeof(FileHeader))
  {
    _outError = LoadError::NotNmo;
    return false;
  }

  /* The bytes move in first, so every pointer taken below already points at
   * the memory the Model will keep. */
  _outModel.m_bytes = std::move(_bytes);
  const std::uint8_t* data = _outModel.m_bytes.data();
  const std::size_t fileBytes = _outModel.m_bytes.size();

  FileHeader header;
  std::memcpy(&header, data, sizeof(header));
  if (header.magic != FileMagic)
  {
    _outError = LoadError::NotNmo;
    _outModel.m_bytes.clear();
    return false;
  }
  if (header.versionMajor != VersionMajor)
  {
    _outError = LoadError::UnsupportedVersion;
    _outModel.m_bytes.clear();
    return false;
  }
  if (header.headerBytes != sizeof(FileHeader) || header.fileBytes != fileBytes)
  {
    _outError = LoadError::Truncated;
    _outModel.m_bytes.clear();
    return false;
  }
  if (header.meshCount > MaxMeshCount)
  {
    _outError = LoadError::BadCount;
    _outModel.m_bytes.clear();
    return false;
  }
  const std::size_t directoryEnd = sizeof(FileHeader) + sizeof(MeshRef) * static_cast<std::size_t>(header.meshCount);
  if (directoryEnd > fileBytes)
  {
    _outError = LoadError::Truncated;
    _outModel.m_bytes.clear();
    return false;
  }
  if (_verifyChecksum && header.payloadCrc32 != 0)
  {
    if (Crc32(data + sizeof(FileHeader), fileBytes - sizeof(FileHeader)) != header.payloadCrc32)
    {
      _outError = LoadError::BadChecksum;
      _outModel.m_bytes.clear();
      return false;
    }
  }

  _outModel.m_meshes.reserve(header.meshCount);
  for (std::uint32_t i = 0; i < header.meshCount; ++i)
  {
    MeshRef ref;
    std::memcpy(&ref, data + sizeof(FileHeader) + sizeof(MeshRef) * static_cast<std::size_t>(i), sizeof(ref));
    const std::uint64_t end = std::uint64_t{ref.offsetBytes} + ref.lengthBytes;
    if (ref.offsetBytes < directoryEnd || end > fileBytes || (ref.offsetBytes % BufferAlignment) != 0)
    {
      _outError = LoadError::BadOffset;
      _outModel.m_bytes.clear();
      _outModel.m_meshes.clear();
      return false;
    }
    MeshView mesh;
    if (!ReadMesh(data + ref.offsetBytes, ref.lengthBytes, mesh, _outError))
    {
      Neuron::DebugTrace("(NmoLoad) mesh {} rejected: {}\n", i, Describe(_outError));
      _outModel.m_bytes.clear();
      _outModel.m_meshes.clear();
      return false;
    }
    _outModel.m_meshes.push_back(std::move(mesh));
  }
  return true;
}

} // namespace Neuron::Nmo
