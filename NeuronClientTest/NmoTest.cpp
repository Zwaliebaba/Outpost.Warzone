#include "pch.h"
#include "CppUnitTest.h"

/*
 * NmoTest.cpp
 *
 * The .nmo loader, against the same golden file the Python codec and the
 * Blender add-on are tested with (NmoFixture.h is generated from it), and
 * against a corrupted copy for every clause of the validation list in
 * Docs/NeuronMeshObject.md §4.11.
 *
 * The rejection tests corrupt the golden bytes rather than carrying 21 more
 * fixtures: each one names the field it breaks, so a reader can see which
 * rule is under test, and the checksum is cleared alongside so the loader
 * gets as far as the rule instead of stopping at the CRC.
 */

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#include "Nmo.h"
#include "NmoLoad.h"

#include "NmoFixture.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron::Nmo;

namespace NeuronClientTest
{

namespace
{

std::vector<std::uint8_t> GoldenBytes()
{
  return std::vector<std::uint8_t>(GoldenNmo, GoldenNmo + GoldenNmoBytes);
}

/* Offsets into the fixture, derived from it rather than written down, so the
 * tests keep pointing at the right fields when the fixture is regenerated. */
std::size_t MeshBlobOffset(const std::vector<std::uint8_t>& _bytes)
{
  MeshRef ref;
  std::memcpy(&ref, _bytes.data() + sizeof(FileHeader), sizeof(ref));
  return ref.offsetBytes;
}

MeshHeader ReadMeshHeader(const std::vector<std::uint8_t>& _bytes)
{
  MeshHeader header;
  std::memcpy(&header, _bytes.data() + MeshBlobOffset(_bytes), sizeof(header));
  return header;
}

std::size_t SubMeshOffset(const std::vector<std::uint8_t>& _bytes, std::uint32_t _index)
{
  const MeshHeader header = ReadMeshHeader(_bytes);
  return MeshBlobOffset(_bytes) + header.subMeshesOffset + sizeof(SubMesh) * _index;
}

SubMesh ReadSubMesh(const std::vector<std::uint8_t>& _bytes, std::uint32_t _index)
{
  SubMesh subMesh;
  std::memcpy(&subMesh, _bytes.data() + SubMeshOffset(_bytes, _index), sizeof(subMesh));
  return subMesh;
}

void WriteSubMesh(std::vector<std::uint8_t>& _bytes, std::uint32_t _index, const SubMesh& _subMesh)
{
  std::memcpy(_bytes.data() + SubMeshOffset(_bytes, _index), &_subMesh, sizeof(_subMesh));
}

/* Clear the payload checksum so a mutation is judged by the rule it breaks
 * and not by the CRC that guards everything after the header. */
void ClearChecksum(std::vector<std::uint8_t>& _bytes)
{
  const std::uint32_t none = 0;
  std::memcpy(_bytes.data() + offsetof(FileHeader, payloadCrc32), &none, sizeof(none));
}

void AssertRejected(std::vector<std::uint8_t> _bytes, LoadError _expected, const wchar_t* _why)
{
  Model model;
  LoadError error = LoadError::None;
  Assert::IsFalse(Model::Load(std::move(_bytes), model, error), _why);
  Assert::AreEqual(static_cast<std::uint32_t>(_expected), static_cast<std::uint32_t>(error), _why);
  Assert::AreEqual(std::size_t{0}, model.MeshCount(), L"a rejected file must leave the model empty");
}

} // namespace

TEST_CLASS(NmoTest)
{
public:
  TEST_METHOD(LoadsTheGoldenModel)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error), L"the golden fixture must load");
    Assert::AreEqual(static_cast<std::uint32_t>(LoadError::None), static_cast<std::uint32_t>(error));
    Assert::AreEqual(std::size_t{1}, model.MeshCount());

    const MeshView& mesh = model.Mesh(0);
    Assert::IsTrue(mesh.name == "RadarTower");
    Assert::AreEqual(std::size_t{2}, mesh.materials.size());
    Assert::AreEqual(std::size_t{2}, mesh.subMeshes.size());
    Assert::AreEqual(std::size_t{1}, mesh.indexBuffers.size());
    Assert::AreEqual(std::size_t{1}, mesh.vertexBuffers.size());
    Assert::AreEqual(std::size_t{1}, mesh.skinBuffers.size());
    Assert::AreEqual(std::uint32_t{12}, mesh.indexBuffers[0].elementCount);
    Assert::AreEqual(std::uint32_t{8}, mesh.vertexBuffers[0].elementCount);
    Assert::AreEqual(VertexStride, mesh.vertexBuffers[0].strideBytes);
    Assert::IsNotNull(mesh.extents);
  }

  TEST_METHOD(ReadsMaterialsAndTheirAtlas)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    Assert::IsTrue(mesh.materials[0].name == "Hull");
    Assert::IsTrue(mesh.materials[0].textures[0] == "page-12-player buildings.dds");
    Assert::AreEqual(static_cast<std::uint32_t>(RenderFlags::AlphaTest), mesh.materials[0].extension->renderFlags);
    Assert::AreEqual(std::uint32_t{0}, mesh.materials[0].extension->atlasFrameCount);

    // The team-colour case: eight frames, selected by player rather than time.
    const MaterialView& dish = mesh.materials[1];
    Assert::IsTrue(dish.name == "DishTeam");
    Assert::AreEqual(static_cast<std::uint32_t>(RenderFlags::DoubleSided), dish.extension->renderFlags);
    Assert::AreEqual(std::uint32_t{8}, dish.extension->atlasFrameCount);
    Assert::AreEqual(std::uint32_t{32}, dish.extension->atlasTileWidthTexels);
    Assert::AreEqual(static_cast<std::uint32_t>(AtlasSelector::Team), dish.extension->atlasSelector);
  }

  TEST_METHOD(ReadsSubMeshNamesFlagsAndRanges)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    const SubMeshView& body = mesh.subMeshes[0];
    Assert::IsTrue(body.name == "Body");
    Assert::AreEqual(std::uint32_t{2}, body.values->primitiveCount);
    Assert::AreEqual(std::uint32_t{0}, body.values->minVertex);
    Assert::AreEqual(std::uint32_t{4}, body.values->vertexCount);
    Assert::AreEqual(static_cast<std::uint32_t>(SubMeshFlags::DeformedAtRuntime), body.values->flags);

    const SubMeshView& dish = mesh.subMeshes[1];
    Assert::IsTrue(dish.name == "Dish");
    Assert::AreEqual(std::uint32_t{6}, dish.values->startIndex);
    Assert::AreEqual(std::uint32_t{4}, dish.values->minVertex);
  }

  TEST_METHOD(ReadsFacetIds)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    // Both triangles of each submesh came from one source quad, which is what
    // lets a tool rebuild the quad exactly rather than guessing a diagonal.
    Assert::IsNotNull(mesh.subMeshes[0].facets);
    Assert::AreEqual(std::uint32_t{0}, mesh.subMeshes[0].facets[0]);
    Assert::AreEqual(mesh.subMeshes[0].facets[0], mesh.subMeshes[0].facets[1]);
    Assert::IsNotNull(mesh.subMeshes[1].facets);
    Assert::AreEqual(std::uint32_t{1}, mesh.subMeshes[1].facets[0]);
  }

  TEST_METHOD(ReadsBonePalettesAndLocalSkeletons)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    Assert::AreEqual(std::size_t{1}, mesh.bones.size());
    Assert::IsTrue(mesh.bones[0].name == "Root");
    Assert::AreEqual(NoBone, mesh.bones[0].meshBoneIndex);

    // The body's table is nothing but an alias: a palette naming the mesh
    // bone it uses, animating nothing of its own.
    const SubMeshView& body = mesh.subMeshes[0];
    Assert::AreEqual(std::size_t{1}, body.bones.size());
    Assert::AreEqual(std::int32_t{0}, body.bones[0].meshBoneIndex);
    Assert::AreEqual(std::size_t{0}, body.clips.size());

    // The dish has a real skeleton: an aliased root and a local child.
    const SubMeshView& dish = mesh.subMeshes[1];
    Assert::AreEqual(std::size_t{2}, dish.bones.size());
    Assert::AreEqual(std::int32_t{0}, dish.bones[0].meshBoneIndex);
    Assert::AreEqual(NoBone, dish.bones[1].meshBoneIndex);
    Assert::AreEqual(std::int32_t{0}, dish.bones[1].values->parentIndex);
  }

  TEST_METHOD(ReadsSubMeshClipsAsSrtTracks)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const SubMeshView& dish = model.Mesh(0).subMeshes[1];

    Assert::AreEqual(std::size_t{1}, dish.clips.size());
    const ClipView& sweep = dish.clips[0];
    Assert::IsTrue(sweep.name == "Sweep");
    Assert::AreEqual(static_cast<std::uint32_t>(KeyEncoding::SrtTracks), static_cast<std::uint32_t>(sweep.encoding));
    Assert::AreEqual(4.0f, sweep.values->endSeconds, 0.0f);
    Assert::AreEqual(std::size_t{1}, sweep.tracks.size());

    const TrackView& track = sweep.tracks[0];
    Assert::AreEqual(std::uint32_t{1}, track.values->boneIndex);
    Assert::AreEqual(std::uint32_t{3}, track.values->rotationKeyCount);
    Assert::AreEqual(0.0f, track.rotation[0].timeSeconds, 0.0f);
    Assert::AreEqual(2.0f, track.rotation[1].timeSeconds, 0.0f);
    Assert::AreEqual(4.0f, track.rotation[2].timeSeconds, 0.0f);
    Assert::AreEqual(std::uint32_t{5}, sweep.values->keyCount);
  }

  TEST_METHOD(ReadsMeshClipsAsCmoMatrixKeys)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    // The CMO encoding is still a first-class citizen: R-NMO-1 asks for bone
    // animation as CMO defines it, and this is literally that.
    Assert::AreEqual(std::size_t{1}, mesh.clips.size());
    Assert::IsTrue(mesh.clips[0].name == "Idle");
    Assert::AreEqual(static_cast<std::uint32_t>(KeyEncoding::MatrixKeys), static_cast<std::uint32_t>(mesh.clips[0].encoding));
    Assert::AreEqual(std::uint32_t{2}, mesh.clips[0].values->keyCount);
    Assert::IsNotNull(mesh.clips[0].keyframes);
    Assert::AreEqual(std::uint32_t{0}, mesh.clips[0].keyframes[0].boneIndex);
    Assert::AreEqual(1.0f, mesh.clips[0].keyframes[1].timeSeconds, 0.0f);
  }

  TEST_METHOD(ReadsMarkersRigidAndBound)
  {
    Model model;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), model, error));
    const MeshView& mesh = model.Mesh(0);

    const SubMeshView& body = mesh.subMeshes[0];
    Assert::AreEqual(std::size_t{2}, body.markers.size());
    Assert::IsTrue(body.markers[0].name == "TurretMount");
    Assert::AreEqual(20.0f, body.markers[0].values->position.y, 0.0f);
    Assert::AreEqual(NoBone, body.markers[0].values->parentBone);
    Assert::IsTrue(body.markers[1].name == "Connector00");

    // A muzzle rides the bone that recoils, which is the whole reason a
    // marker carries a bone index and an orientation.
    const SubMeshView& dish = mesh.subMeshes[1];
    Assert::AreEqual(std::size_t{1}, dish.markers.size());
    Assert::IsTrue(dish.markers[0].name == "Muzzle0");
    Assert::AreEqual(std::int32_t{1}, dish.markers[0].values->parentBone);
    Assert::AreEqual(24.0f, dish.markers[0].values->position.y, 0.0f);
  }

  TEST_METHOD(MovingAModelKeepsItsViewsValid)
  {
    Model first;
    LoadError error = LoadError::None;
    Assert::IsTrue(Model::Load(GoldenBytes(), first, error));
    const void* vertices = first.Mesh(0).vertexBuffers[0].data;

    Model second = std::move(first);
    Assert::AreEqual(std::size_t{1}, second.MeshCount());
    Assert::IsTrue(second.Mesh(0).name == "RadarTower");
    Assert::IsTrue(vertices == second.Mesh(0).vertexBuffers[0].data);
  }

  // --- rejection: one per clause of the validation list -------------------

  TEST_METHOD(RejectsWrongMagic)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    bytes[0] = 'X';
    AssertRejected(std::move(bytes), LoadError::NotNmo, L"a file that is not an NMO must be refused");
  }

  TEST_METHOD(RejectsAnUnsupportedMajorVersion)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    const std::uint16_t next = VersionMajor + 1;
    std::memcpy(bytes.data() + offsetof(FileHeader, versionMajor), &next, sizeof(next));
    AssertRejected(std::move(bytes), LoadError::UnsupportedVersion, L"a future major version must be refused");
  }

  TEST_METHOD(RejectsAFileSizeThatDisagreesWithTheHeader)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    const std::uint32_t wrong = static_cast<std::uint32_t>(bytes.size()) + 16;
    std::memcpy(bytes.data() + offsetof(FileHeader, fileBytes), &wrong, sizeof(wrong));
    AssertRejected(std::move(bytes), LoadError::Truncated, L"fileBytes must match the bytes actually read");
  }

  TEST_METHOD(RejectsATruncatedFile)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    bytes.resize(bytes.size() / 2);
    AssertRejected(std::move(bytes), LoadError::Truncated, L"half a file is not a file");
  }

  TEST_METHOD(RejectsAMeshWindowOutsideTheFile)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes); // the mesh directory is inside the checksummed payload
    const std::uint32_t huge = static_cast<std::uint32_t>(bytes.size()) * 4;
    std::memcpy(bytes.data() + sizeof(FileHeader) + offsetof(MeshRef, lengthBytes), &huge, sizeof(huge));
    AssertRejected(std::move(bytes), LoadError::BadOffset, L"a mesh may not claim bytes the file does not have");
  }

  TEST_METHOD(RejectsAnOversizeMeshCount)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    const std::uint32_t absurd = MaxMeshCount + 1;
    std::memcpy(bytes.data() + offsetof(FileHeader, meshCount), &absurd, sizeof(absurd));
    AssertRejected(std::move(bytes), LoadError::BadCount, L"a count must be capped before anything is allocated");
  }

  TEST_METHOD(RejectsAChecksumMismatch)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    const std::uint32_t wrong = 0xDEADBEEF;
    std::memcpy(bytes.data() + offsetof(FileHeader, payloadCrc32), &wrong, sizeof(wrong));
    AssertRejected(std::move(bytes), LoadError::BadChecksum, L"the payload checksum must be honoured");
  }

  TEST_METHOD(RejectsAnIndexRangePastItsBuffer)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    body.primitiveCount = 99;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::BadDrawRange, L"a submesh may not read past its index buffer");
  }

  TEST_METHOD(RejectsAnIndexOutsideTheVertexWindow)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh dish = ReadSubMesh(bytes, 1);
    dish.minVertex = 0;
    dish.vertexCount = 2;
    WriteSubMesh(bytes, 1, dish);
    AssertRejected(std::move(bytes), LoadError::BadDrawRange, L"every index must land in the declared vertex window");
  }

  TEST_METHOD(RejectsAMaterialIndexOutOfRange)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    body.materialIndex = 7;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::BadDrawRange, L"a submesh may not name a material that is not there");
  }

  TEST_METHOD(RejectsAMisalignedOffset)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    body.markersOffset += 1;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::BadOffset, L"record streams must be four-byte aligned");
  }

  TEST_METHOD(RejectsAnOffsetOutsideTheMesh)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    body.bonesOffset = 0x00FFFFF0;
    body.boneCount = 1;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::BadOffset, L"an offset must stay inside its own mesh blob");
  }

  TEST_METHOD(RejectsAnOversizeBoneCount)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    body.boneCount = MaxBoneCount + 1;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::BadCount, L"a bone count must be capped before it is trusted");
  }

  TEST_METHOD(RejectsAFacetCountThatDisagreesWithTheTriangles)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    SubMesh body = ReadSubMesh(bytes, 0);
    // Point the facet section at the last four bytes of the blob: there is
    // then room for one id where primitiveCount asks for two.
    MeshRef ref;
    std::memcpy(&ref, bytes.data() + sizeof(FileHeader), sizeof(ref));
    body.facetsOffset = ref.lengthBytes - 4;
    WriteSubMesh(bytes, 0, body);
    AssertRejected(std::move(bytes), LoadError::Truncated, L"the facet section must hold one id per triangle");
  }

  TEST_METHOD(RejectsABoneParentThatIsNotAnEarlierBone)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    // The dish's second bone parents onto its first; make it parent onto
    // itself, which no forward pass could evaluate.
    const SubMesh dish = ReadSubMesh(bytes, 1);
    const std::size_t bones = MeshBlobOffset(bytes) + dish.bonesOffset;
    const std::size_t second = FindSecondBoneParent(bytes, bones);
    const std::int32_t self = 1;
    std::memcpy(bytes.data() + second, &self, sizeof(self));
    AssertRejected(std::move(bytes), LoadError::BadBone, L"a bone's parent must come before it");
  }

  TEST_METHOD(RejectsAMeshBoneCarryingAnAlias)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const MeshHeader header = ReadMeshHeader(bytes);
    // The mesh skeleton has nothing to alias, so any value but NoBone is a
    // file that means something the format cannot express.
    const std::size_t record = MeshBlobOffset(bytes) + header.bonesOffset;
    const std::size_t alias = SkipString(bytes, record) + sizeof(Bone);
    const std::int32_t zero = 0;
    std::memcpy(bytes.data() + alias, &zero, sizeof(zero));
    AssertRejected(std::move(bytes), LoadError::BadBone, L"a mesh-scope bone may not alias anything");
  }

  TEST_METHOD(RejectsAClipKeyingABoneOutsideItsScope)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const SubMesh dish = ReadSubMesh(bytes, 1);
    const std::size_t clip = MeshBlobOffset(bytes) + dish.clipsOffset;
    const std::size_t track = SkipString(bytes, clip) + sizeof(Clip) + sizeof(ClipTracks);
    const std::uint32_t outside = 9;
    std::memcpy(bytes.data() + track + offsetof(SrtTrack, boneIndex), &outside, sizeof(outside));
    AssertRejected(std::move(bytes), LoadError::BadClip, L"a track may only key a bone its scope has");
  }

  TEST_METHOD(RejectsUnsortedKeyTimes)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const SubMesh dish = ReadSubMesh(bytes, 1);
    const std::size_t clip = MeshBlobOffset(bytes) + dish.clipsOffset;
    const std::size_t track = SkipString(bytes, clip) + sizeof(Clip) + sizeof(ClipTracks);
    // Rotation keys follow the single translation key.
    const std::size_t rotation = track + sizeof(SrtTrack) + sizeof(TranslationKey);
    const float backwards = -1.0f;
    std::memcpy(bytes.data() + rotation + sizeof(RotationKey) + offsetof(RotationKey, timeSeconds), &backwards, sizeof(backwards));
    AssertRejected(std::move(bytes), LoadError::BadClip, L"keys within a track must increase in time");
  }

  TEST_METHOD(RejectsAnUnknownKeyEncoding)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const SubMesh dish = ReadSubMesh(bytes, 1);
    const std::size_t clip = MeshBlobOffset(bytes) + dish.clipsOffset;
    const std::size_t framing = SkipString(bytes, clip) + sizeof(Clip);
    const std::uint32_t unknown = 7;
    std::memcpy(bytes.data() + framing + offsetof(ClipTracks, encoding), &unknown, sizeof(unknown));
    AssertRejected(std::move(bytes), LoadError::BadClip, L"an unsizable payload must not be skipped past");
  }

  TEST_METHOD(RejectsAMarkerBoundToABoneOutsideItsScope)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const SubMesh dish = ReadSubMesh(bytes, 1);
    const std::size_t record = MeshBlobOffset(bytes) + dish.markersOffset;
    const std::size_t marker = SkipString(bytes, record);
    const std::int32_t outside = 9;
    std::memcpy(bytes.data() + marker + offsetof(Marker, parentBone), &outside, sizeof(outside));
    AssertRejected(std::move(bytes), LoadError::BadMarker, L"a marker may only bind a bone its scope has");
  }

  TEST_METHOD(RejectsADuplicateMarkerName)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const SubMesh body = ReadSubMesh(bytes, 0);
    const std::size_t first = MeshBlobOffset(bytes) + body.markersOffset;
    const std::size_t second = SkipString(bytes, first) + sizeof(Marker);
    // "TurretMount" and "Connector00" are both eleven bytes, so the second
    // name can be overwritten with the first in place.
    std::memcpy(bytes.data() + second + sizeof(std::uint32_t), bytes.data() + first + sizeof(std::uint32_t), 11);
    AssertRejected(std::move(bytes), LoadError::BadMarker, L"marker names must be unique within a submesh");
  }

  TEST_METHOD(RejectsAnOversizeString)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const MeshHeader header = ReadMeshHeader(bytes);
    const std::uint32_t huge = MaxStringBytes + 1;
    std::memcpy(bytes.data() + MeshBlobOffset(bytes) + header.nameOffset, &huge, sizeof(huge));
    AssertRejected(std::move(bytes), LoadError::BadString, L"a name may not exceed MaxStringBytes");
  }

  TEST_METHOD(RejectsInvalidUtf8InAName)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const MeshHeader header = ReadMeshHeader(bytes);
    bytes[MeshBlobOffset(bytes) + header.nameOffset + sizeof(std::uint32_t)] = 0xFF;
    AssertRejected(std::move(bytes), LoadError::BadString, L"a name must be valid UTF-8");
  }

  TEST_METHOD(RejectsABufferStrideThatDoesNotMatchItsFormat)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const MeshHeader header = ReadMeshHeader(bytes);
    const std::size_t buffer = MeshBlobOffset(bytes) + header.vertexBuffersOffset;
    const std::uint32_t wrong = VertexStride + 4;
    std::memcpy(bytes.data() + buffer + offsetof(BufferHeader, strideBytes), &wrong, sizeof(wrong));
    AssertRejected(std::move(bytes), LoadError::BadBuffer, L"a stride must match the format it declares");
  }

  TEST_METHOD(RejectsASkinBufferThatDoesNotPairWithItsVertexBuffer)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    MeshHeader header = ReadMeshHeader(bytes);
    header.skinBufferCount = 2; // one vertex buffer, so two skin buffers cannot pair
    std::memcpy(bytes.data() + MeshBlobOffset(bytes), &header, sizeof(header));
    AssertRejected(std::move(bytes), LoadError::BadBuffer, L"skin buffers pair one-to-one with vertex buffers");
  }

  TEST_METHOD(RejectsASkinIndexOutsideItsBoneScope)
  {
    std::vector<std::uint8_t> bytes = GoldenBytes();
    ClearChecksum(bytes);
    const MeshHeader header = ReadMeshHeader(bytes);
    const std::size_t payload = MeshBlobOffset(bytes) + header.skinBuffersOffset + sizeof(BufferHeader);
    const std::uint32_t outside = 9;
    std::memcpy(bytes.data() + payload + offsetof(SkinVertex, boneIndex), &outside, sizeof(outside));
    AssertRejected(std::move(bytes), LoadError::BadSkin, L"a weighted influence must name a bone in scope");
  }

private:
  /* Step over a String record: a uint32 length, its bytes, and the padding
   * up to the next four-byte boundary. */
  static std::size_t SkipString(const std::vector<std::uint8_t>& _bytes, std::size_t _at)
  {
    std::uint32_t length = 0;
    std::memcpy(&length, _bytes.data() + _at, sizeof(length));
    const std::size_t end = _at + sizeof(length) + length;
    return (end + RecordAlignment - 1) & ~(static_cast<std::size_t>(RecordAlignment) - 1);
  }

  /* The parentIndex field of the second bone record in a table. */
  static std::size_t FindSecondBoneParent(const std::vector<std::uint8_t>& _bytes, std::size_t _table)
  {
    const std::size_t firstRecordEnd = SkipString(_bytes, _table) + sizeof(Bone) + sizeof(std::int32_t);
    return SkipString(_bytes, firstRecordEnd) + offsetof(Bone, parentIndex);
  }
};

} // namespace NeuronClientTest
