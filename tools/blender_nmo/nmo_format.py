#!/usr/bin/env python3
"""Reference reader/writer for the Neuron Mesh Object (.nmo) binary format.

This module is the executable statement of Docs/NeuronMeshObject.md.  It has
no Blender dependency, so it serves three callers:

  * the Blender add-on in this directory (import_nmo.py / export_nmo.py),
  * the .pie converter (tools/pie_to_nmo.py),
  * the round-trip test (tools/nmo_roundtrip_test.py).

Everything is little-endian, naturally aligned and packing-pragma free, so
struct sizes here must agree with the C++ static_asserts in the design
document.  SIZES below is checked against struct.calcsize at import time.

Loading follows the validation order of the specification: nothing is trusted
until it is bounds-checked, and a malformed file raises NmoError instead of
being repaired.
"""

import struct
import zlib

FILE_MAGIC = 0x314F4D4E              # 'NMO1' on disk
VERSION_MAJOR = 1
VERSION_MINOR = 0
MAX_STRING_BYTES = 1024
TEXTURE_SLOTS = 8
BONE_INFLUENCES = 4
NO_PARENT = -1
NO_BONE = -1

# Section alignment: buffers 16, record streams 4.
BUFFER_ALIGN = 16
RECORD_ALIGN = 4

# BufferHeader.format values and their mandatory strides.
INDEX_U16, INDEX_U32 = 0, 1
VERTEX_STANDARD = 0
SKIN_STANDARD = 0
INDEX_STRIDE = {INDEX_U16: 2, INDEX_U32: 4}
VERTEX_STRIDE = {VERTEX_STANDARD: 52}
SKIN_STRIDE = {SKIN_STANDARD: 32}

# Clip key encodings.
CLIP_MATRIX_KEYS = 0                 # CMO Keyframe[]; what CMO itself stores
CLIP_SRT_TRACKS = 1                  # per-bone translation/rotation/scale keys

# MaterialExt.renderFlags
MAT_DOUBLE_SIDED = 0x1
MAT_ALPHA_TEST = 0x2                 # colour-keyed texel is transparent
MAT_ADDITIVE = 0x4

# MaterialExt.atlasSelector
ATLAS_NONE, ATLAS_TIME, ATLAS_TEAM = 0, 1, 2

# SubMesh.flags
SUB_DEFORMED_AT_RUNTIME = 0x1        # engine rewrites vertices (terrain conform)

# Struct formats.  '<' is little-endian and, critically, no implicit padding,
# so every layout below is stated rather than inherited from the host ABI.
F_FILE_HEADER = '<IHHIIIIII'
F_MESH_REF = '<II'
F_MESH_HEADER = '<24I'
F_MATERIAL = '<17f'                  # 4+4+4+1+4 floats then a 4x4 matrix = 33
F_MATERIAL_FULL = '<13f' + '16f'
F_MATERIAL_EXT = '<8I'
F_BUFFER_HEADER = '<4I'
F_VERTEX = '<3f3f4fI2f'
F_SKIN_VERTEX = '<4I4f'
F_EXTENTS = '<10f'
F_SUBMESH = '<17I10f5I'
F_BONE = '<i32f'                     # parentIndex + 3 matrices... see BONE below
F_CLIP = '<ffI'
F_CLIP_TRACKS = '<II'
F_KEYFRAME = '<If16f'
F_SRT_TRACK = '<4I'
F_KEY_T = '<f3f'
F_KEY_R = '<f4f'
F_KEY_S = '<ff'
F_MARKER = '<3f4ffiI'

SIZE_FILE_HEADER = 32
SIZE_MESH_REF = 8
SIZE_MESH_HEADER = 96
SIZE_MATERIAL = 132
SIZE_MATERIAL_EXT = 32
SIZE_BUFFER_HEADER = 16
SIZE_VERTEX = 52
SIZE_SKIN_VERTEX = 32
SIZE_EXTENTS = 40
SIZE_SUBMESH = 128
SIZE_BONE = 196
SIZE_CLIP = 12
SIZE_CLIP_TRACKS = 8
SIZE_KEYFRAME = 72
SIZE_SRT_TRACK = 16
SIZE_KEY_T = 16
SIZE_KEY_R = 20
SIZE_KEY_S = 8
SIZE_MARKER = 40

_MATERIAL_FMT = '<4f4f4ff4f16f'       # ambient, diffuse, specular, power, emissive, uv
_BONE_FMT = '<i16f16f16f'

_CHECKS = [
    (F_FILE_HEADER, SIZE_FILE_HEADER, 'FileHeader'),
    (F_MESH_REF, SIZE_MESH_REF, 'MeshRef'),
    (F_MESH_HEADER, SIZE_MESH_HEADER, 'MeshHeader'),
    (_MATERIAL_FMT, SIZE_MATERIAL, 'Material'),
    (F_MATERIAL_EXT, SIZE_MATERIAL_EXT, 'MaterialExt'),
    (F_BUFFER_HEADER, SIZE_BUFFER_HEADER, 'BufferHeader'),
    (F_VERTEX, SIZE_VERTEX, 'Vertex'),
    (F_SKIN_VERTEX, SIZE_SKIN_VERTEX, 'SkinVertex'),
    (F_EXTENTS, SIZE_EXTENTS, 'MeshExtents'),
    (F_SUBMESH, SIZE_SUBMESH, 'SubMesh'),
    (_BONE_FMT, SIZE_BONE, 'Bone'),
    (F_CLIP, SIZE_CLIP, 'Clip'),
    (F_CLIP_TRACKS, SIZE_CLIP_TRACKS, 'ClipTracks'),
    (F_KEYFRAME, SIZE_KEYFRAME, 'Keyframe'),
    (F_SRT_TRACK, SIZE_SRT_TRACK, 'SrtTrack'),
    (F_KEY_T, SIZE_KEY_T, 'TranslationKey'),
    (F_KEY_R, SIZE_KEY_R, 'RotationKey'),
    (F_KEY_S, SIZE_KEY_S, 'ScaleKey'),
    (F_MARKER, SIZE_MARKER, 'Marker'),
]
for _fmt, _size, _name in _CHECKS:
    if struct.calcsize(_fmt) != _size:
        raise AssertionError('%s: layout %d bytes, spec says %d' % (_name, struct.calcsize(_fmt), _size))

IDENTITY_4X4 = (1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0)

DEFAULT_MATERIAL_VALUES = dict(
    ambient=(0.2, 0.2, 0.2, 1.0),
    diffuse=(0.8, 0.8, 0.8, 1.0),
    specular=(0.0, 0.0, 0.0, 1.0),
    specular_power=1.0,
    emissive=(0.0, 0.0, 0.0, 1.0),
    uv_transform=IDENTITY_4X4,
)


class NmoError(Exception):
    """A file that does not conform.  Loaders reject; they never repair."""


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


# --------------------------------------------------------------------------
# Data model.  Plain containers: the codec owns the bytes, callers own meaning.
# --------------------------------------------------------------------------

class Material:
    def __init__(self, name='', textures=None, shader='', render_flags=0,
                 atlas_frame_count=0, atlas_tile_width=0, atlas_tile_height=0,
                 atlas_frames_per_row=0, atlas_selector=ATLAS_NONE, atlas_frame_ms=0,
                 **values):
        self.name = name
        self.textures = list(textures) if textures else [''] * TEXTURE_SLOTS
        while len(self.textures) < TEXTURE_SLOTS:
            self.textures.append('')
        self.shader = shader
        self.render_flags = render_flags
        self.atlas_frame_count = atlas_frame_count
        self.atlas_tile_width = atlas_tile_width
        self.atlas_tile_height = atlas_tile_height
        self.atlas_frames_per_row = atlas_frames_per_row
        self.atlas_selector = atlas_selector
        self.atlas_frame_ms = atlas_frame_ms
        for key, default in DEFAULT_MATERIAL_VALUES.items():
            setattr(self, key, tuple(values.get(key, default)) if isinstance(default, tuple) else values.get(key, default))


class Bone:
    def __init__(self, name='', parent_index=NO_PARENT, inv_bind_pose=IDENTITY_4X4,
                 bind_pose=IDENTITY_4X4, local_transform=IDENTITY_4X4, mesh_bone_index=NO_BONE):
        self.name = name
        self.parent_index = parent_index
        self.inv_bind_pose = tuple(inv_bind_pose)
        self.bind_pose = tuple(bind_pose)
        self.local_transform = tuple(local_transform)
        self.mesh_bone_index = mesh_bone_index   # >= 0 aliases a mesh bone


class SrtTrack:
    def __init__(self, bone_index=0, translation=None, rotation=None, scale=None):
        self.bone_index = bone_index
        self.translation = list(translation or [])   # (t, (x, y, z))
        self.rotation = list(rotation or [])         # (t, (x, y, z, w))
        self.scale = list(scale or [])               # (t, s)

    def key_count(self):
        return len(self.translation) + len(self.rotation) + len(self.scale)


class Clip:
    def __init__(self, name='', start_seconds=0.0, end_seconds=0.0,
                 encoding=CLIP_SRT_TRACKS, tracks=None, keyframes=None):
        self.name = name
        self.start_seconds = start_seconds
        self.end_seconds = end_seconds
        self.encoding = encoding
        self.tracks = list(tracks or [])             # SrtTrack, when encoding == SRT
        self.keyframes = list(keyframes or [])       # (bone, time, 16 floats), when MATRIX

    def key_count(self):
        if self.encoding == CLIP_SRT_TRACKS:
            return sum(t.key_count() for t in self.tracks)
        return len(self.keyframes)


class Marker:
    def __init__(self, name='', position=(0.0, 0.0, 0.0), orientation=(0.0, 0.0, 0.0, 1.0),
                 scale=1.0, parent_bone=NO_BONE, flags=0):
        self.name = name
        self.position = tuple(position)
        self.orientation = tuple(orientation)
        self.scale = scale
        self.parent_bone = parent_bone
        self.flags = flags


class Extents:
    def __init__(self, center=(0.0, 0.0, 0.0), radius=0.0,
                 box_min=(0.0, 0.0, 0.0), box_max=(0.0, 0.0, 0.0)):
        self.center = tuple(center)
        self.radius = radius
        self.box_min = tuple(box_min)
        self.box_max = tuple(box_max)

    @staticmethod
    def from_points(points):
        if not points:
            return Extents()
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        zs = [p[2] for p in points]
        box_min = (min(xs), min(ys), min(zs))
        box_max = (max(xs), max(ys), max(zs))
        center = tuple((box_min[i] + box_max[i]) * 0.5 for i in range(3))
        radius = max(((p[0] - center[0]) ** 2 + (p[1] - center[1]) ** 2 + (p[2] - center[2]) ** 2) ** 0.5
                     for p in points)
        return Extents(center, radius, box_min, box_max)


class SubMesh:
    def __init__(self, material_index=0, index_buffer_index=0, vertex_buffer_index=0,
                 start_index=0, primitive_count=0, base_vertex=0, min_vertex=0,
                 vertex_count=0, flags=0, bones=None, clips=None, markers=None,
                 facets=None, extents=None, name=''):
        self.name = name                   # role and .ani binding travel in this
        self.material_index = material_index
        self.index_buffer_index = index_buffer_index
        self.vertex_buffer_index = vertex_buffer_index
        self.start_index = start_index
        self.primitive_count = primitive_count
        self.base_vertex = base_vertex
        self.min_vertex = min_vertex
        self.vertex_count = vertex_count
        self.flags = flags
        self.bones = list(bones or [])
        self.clips = list(clips or [])
        self.markers = list(markers or [])
        self.facets = list(facets or [])   # one source-polygon id per triangle
        self.extents = extents or Extents()


class Mesh:
    def __init__(self, name='', flags=0):
        self.name = name
        self.flags = flags
        self.materials = []
        self.sub_meshes = []
        self.index_buffers = []            # list of (format, [indices])
        self.vertex_buffers = []           # list of [vertex tuples]
        self.skin_buffers = []             # list of [(indices4, weights4)] or []
        self.extents = Extents()
        self.bones = []
        self.clips = []


class Model:
    def __init__(self, meshes=None, flags=0, version_minor=VERSION_MINOR):
        self.meshes = list(meshes or [])
        self.flags = flags
        self.version_minor = version_minor


# --------------------------------------------------------------------------
# Writing
# --------------------------------------------------------------------------

class _Writer:
    def __init__(self):
        self.buf = bytearray()

    def tell(self):
        return len(self.buf)

    def raw(self, data):
        self.buf += data

    def pad_to(self, alignment):
        self.buf += b'\0' * (_align(len(self.buf), alignment) - len(self.buf))

    def string(self, text):
        data = text.encode('utf-8')
        if len(data) > MAX_STRING_BYTES:
            raise NmoError('string of %d bytes exceeds MaxStringBytes' % len(data))
        self.buf += struct.pack('<I', len(data)) + data
        self.pad_to(RECORD_ALIGN)


def _pack_material(w, mat):
    w.string(mat.name)
    w.raw(struct.pack(_MATERIAL_FMT, *mat.ambient, *mat.diffuse, *mat.specular,
                      mat.specular_power, *mat.emissive, *mat.uv_transform))
    w.raw(struct.pack(F_MATERIAL_EXT, mat.render_flags, mat.atlas_frame_count,
                      mat.atlas_tile_width, mat.atlas_tile_height,
                      mat.atlas_frames_per_row, mat.atlas_selector,
                      mat.atlas_frame_ms, 0))
    w.string(mat.shader)
    for i in range(TEXTURE_SLOTS):
        w.string(mat.textures[i])


def _pack_bone(w, bone):
    w.string(bone.name)
    w.raw(struct.pack(_BONE_FMT, bone.parent_index, *bone.inv_bind_pose,
                      *bone.bind_pose, *bone.local_transform))
    w.raw(struct.pack('<i', bone.mesh_bone_index))


def _pack_clip(w, clip):
    w.string(clip.name)
    w.raw(struct.pack(F_CLIP, clip.start_seconds, clip.end_seconds, clip.key_count()))
    track_count = len(clip.tracks) if clip.encoding == CLIP_SRT_TRACKS else 0
    w.raw(struct.pack(F_CLIP_TRACKS, clip.encoding, track_count))
    if clip.encoding == CLIP_MATRIX_KEYS:
        for bone_index, time, transform in clip.keyframes:
            w.raw(struct.pack(F_KEYFRAME, bone_index, time, *transform))
        return
    for track in clip.tracks:
        w.raw(struct.pack(F_SRT_TRACK, track.bone_index, len(track.translation),
                          len(track.rotation), len(track.scale)))
    for track in clip.tracks:
        for time, value in track.translation:
            w.raw(struct.pack(F_KEY_T, time, *value))
        for time, value in track.rotation:
            w.raw(struct.pack(F_KEY_R, time, *value))
        for time, value in track.scale:
            w.raw(struct.pack(F_KEY_S, time, value))


def _pack_marker(w, marker):
    w.string(marker.name)
    w.raw(struct.pack(F_MARKER, *marker.position, *marker.orientation,
                      marker.scale, marker.parent_bone, marker.flags))


def _pack_extents(ext):
    return struct.pack(F_EXTENTS, *ext.center, ext.radius, *ext.box_min, *ext.box_max)


def _write_mesh(mesh):
    """Serialise one mesh blob.  Offsets are blob-relative, per the spec."""
    w = _Writer()
    w.raw(b'\0' * SIZE_MESH_HEADER)

    name_offset = w.tell()
    w.string(mesh.name)

    materials_offset = w.tell() if mesh.materials else 0
    for mat in mesh.materials:
        _pack_material(w, mat)

    # The submesh table is fixed-stride, so its records are written after the
    # variable-size streams they point at.
    sub_streams = []
    for sub in mesh.sub_meshes:
        name_off = w.tell() if sub.name else 0
        if sub.name:
            w.string(sub.name)
        bones_offset = w.tell() if sub.bones else 0
        for bone in sub.bones:
            _pack_bone(w, bone)
        clips_offset = w.tell() if sub.clips else 0
        for clip in sub.clips:
            _pack_clip(w, clip)
        markers_offset = w.tell() if sub.markers else 0
        for marker in sub.markers:
            _pack_marker(w, marker)
        facets_offset = 0
        if sub.facets:
            if len(sub.facets) != sub.primitive_count:
                raise NmoError('facet list is %d long, primitiveCount is %d'
                               % (len(sub.facets), sub.primitive_count))
            facets_offset = w.tell()
            w.raw(struct.pack('<%dI' % len(sub.facets), *sub.facets))
        sub_streams.append((name_off, bones_offset, clips_offset, markers_offset, facets_offset))

    w.pad_to(RECORD_ALIGN)
    sub_meshes_offset = w.tell() if mesh.sub_meshes else 0
    for sub, (no, bo, co, mo, fo) in zip(mesh.sub_meshes, sub_streams):
        w.raw(struct.pack(F_SUBMESH,
                          sub.material_index, sub.index_buffer_index, sub.vertex_buffer_index,
                          sub.start_index, sub.primitive_count, sub.base_vertex,
                          sub.min_vertex, sub.vertex_count, sub.flags, no,
                          len(sub.bones), bo, len(sub.clips), co,
                          len(sub.markers), mo, fo,
                          *sub.extents.center, sub.extents.radius,
                          *sub.extents.box_min, *sub.extents.box_max,
                          0, 0, 0, 0, 0))

    def write_buffers(entries, pack_element, fmt_of):
        w.pad_to(BUFFER_ALIGN)
        offset = w.tell() if entries else 0
        for entry in entries:
            w.pad_to(BUFFER_ALIGN)
            fmt, stride, elements = fmt_of(entry)
            w.raw(struct.pack(F_BUFFER_HEADER, fmt, stride, len(elements), 0))
            for element in elements:
                w.raw(pack_element(fmt, element))
            w.pad_to(BUFFER_ALIGN)
        return offset

    index_buffers_offset = write_buffers(
        mesh.index_buffers,
        lambda fmt, value: struct.pack('<H' if fmt == INDEX_U16 else '<I', value),
        lambda entry: (entry[0], INDEX_STRIDE[entry[0]], entry[1]))
    vertex_buffers_offset = write_buffers(
        mesh.vertex_buffers,
        lambda fmt, v: struct.pack(F_VERTEX, *v[0], *v[1], *v[2], v[3], *v[4]),
        lambda entry: (VERTEX_STANDARD, SIZE_VERTEX, entry))
    skin_buffers_offset = write_buffers(
        mesh.skin_buffers,
        lambda fmt, s: struct.pack(F_SKIN_VERTEX, *s[0], *s[1]),
        lambda entry: (SKIN_STANDARD, SIZE_SKIN_VERTEX, entry))

    w.pad_to(RECORD_ALIGN)
    extents_offset = w.tell()
    w.raw(_pack_extents(mesh.extents))

    bones_offset = w.tell() if mesh.bones else 0
    for bone in mesh.bones:
        _pack_bone(w, bone)
    clips_offset = w.tell() if mesh.clips else 0
    for clip in mesh.clips:
        _pack_clip(w, clip)
    w.pad_to(BUFFER_ALIGN)

    header = struct.pack(F_MESH_HEADER,
                         mesh.flags, name_offset,
                         len(mesh.materials), materials_offset,
                         len(mesh.sub_meshes), sub_meshes_offset,
                         len(mesh.index_buffers), index_buffers_offset,
                         len(mesh.vertex_buffers), vertex_buffers_offset,
                         len(mesh.skin_buffers), skin_buffers_offset,
                         extents_offset,
                         len(mesh.bones), bones_offset,
                         len(mesh.clips), clips_offset,
                         0, 0, 0, 0, 0, 0, 0)
    w.buf[0:SIZE_MESH_HEADER] = header
    return bytes(w.buf)


def write_nmo(model, compute_crc=True):
    """Serialise a Model to .nmo bytes."""
    blobs = [_write_mesh(mesh) for mesh in model.meshes]
    directory_bytes = SIZE_MESH_REF * len(blobs)
    cursor = _align(SIZE_FILE_HEADER + directory_bytes, BUFFER_ALIGN)
    refs = []
    for blob in blobs:
        refs.append((cursor, len(blob)))
        cursor = _align(cursor + len(blob), BUFFER_ALIGN)
    total = cursor

    out = bytearray(total)
    out[SIZE_FILE_HEADER:SIZE_FILE_HEADER + directory_bytes] = b''.join(
        struct.pack(F_MESH_REF, off, length) for off, length in refs)
    for (off, length), blob in zip(refs, blobs):
        out[off:off + length] = blob

    crc = zlib.crc32(bytes(out[SIZE_FILE_HEADER:])) & 0xFFFFFFFF if compute_crc else 0
    out[0:SIZE_FILE_HEADER] = struct.pack(F_FILE_HEADER, FILE_MAGIC, VERSION_MAJOR,
                                          model.version_minor, SIZE_FILE_HEADER, total,
                                          len(blobs), model.flags, crc, 0)
    return bytes(out)


# --------------------------------------------------------------------------
# Reading
# --------------------------------------------------------------------------

class _Reader:
    def __init__(self, data, base, limit):
        self.data = data
        self.base = base        # blob start; offsets in records are relative to it
        self.limit = limit      # one past the blob's last byte, absolute

    def at(self, offset):
        pos = self.base + offset
        if offset == 0 or not (self.base <= pos < self.limit):
            raise NmoError('offset %d outside its mesh blob' % offset)
        return pos

    def need(self, pos, size):
        if size < 0 or pos + size > self.limit:
            raise NmoError('record of %d bytes runs past the mesh blob' % size)
        return pos + size

    def unpack(self, fmt, pos):
        size = struct.calcsize(fmt)
        self.need(pos, size)
        return struct.unpack_from(fmt, self.data, pos), pos + size

    def string(self, pos):
        (length,), pos = self.unpack('<I', pos)
        if length > MAX_STRING_BYTES:
            raise NmoError('string length %d exceeds MaxStringBytes' % length)
        self.need(pos, length)
        raw = self.data[pos:pos + length]
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError as exc:
            raise NmoError('string is not valid UTF-8: %s' % exc)
        return text, _align(pos + length, RECORD_ALIGN)


def _read_material(r, pos):
    name, pos = r.string(pos)
    values, pos = r.unpack(_MATERIAL_FMT, pos)
    ext, pos = r.unpack(F_MATERIAL_EXT, pos)
    shader, pos = r.string(pos)
    textures = []
    for _ in range(TEXTURE_SLOTS):
        tex, pos = r.string(pos)
        textures.append(tex)
    mat = Material(name=name, textures=textures, shader=shader,
                   render_flags=ext[0], atlas_frame_count=ext[1],
                   atlas_tile_width=ext[2], atlas_tile_height=ext[3],
                   atlas_frames_per_row=ext[4], atlas_selector=ext[5],
                   atlas_frame_ms=ext[6],
                   ambient=values[0:4], diffuse=values[4:8], specular=values[8:12],
                   specular_power=values[12], emissive=values[13:17],
                   uv_transform=values[17:33])
    return mat, pos


def _read_bone(r, pos):
    name, pos = r.string(pos)
    values, pos = r.unpack(_BONE_FMT, pos)
    (mesh_bone_index,), pos = r.unpack('<i', pos)
    bone = Bone(name=name, parent_index=values[0], inv_bind_pose=values[1:17],
                bind_pose=values[17:33], local_transform=values[33:49],
                mesh_bone_index=mesh_bone_index)
    return bone, pos


def _read_clip(r, pos):
    name, pos = r.string(pos)
    (start, end, key_count), pos = r.unpack(F_CLIP, pos)
    (encoding, track_count), pos = r.unpack(F_CLIP_TRACKS, pos)
    if end < start:
        raise NmoError('clip %r ends before it starts' % name)
    clip = Clip(name=name, start_seconds=start, end_seconds=end, encoding=encoding)
    if encoding == CLIP_MATRIX_KEYS:
        if track_count:
            raise NmoError('clip %r uses matrix keys but declares %d tracks' % (name, track_count))
        last = None
        for _ in range(key_count):
            values, pos = r.unpack(F_KEYFRAME, pos)
            key = (values[0], values[1], values[2:18])
            if last is not None and (key[0], key[1]) <= (last[0], last[1]):
                raise NmoError('clip %r keyframes are not sorted by (bone, time)' % name)
            last = key
            clip.keyframes.append(key)
    elif encoding == CLIP_SRT_TRACKS:
        counts = []
        for _ in range(track_count):
            values, pos = r.unpack(F_SRT_TRACK, pos)
            counts.append(values)
        total = 0
        for bone_index, nt, nr, ns in counts:
            track = SrtTrack(bone_index)
            for _ in range(nt):
                v, pos = r.unpack(F_KEY_T, pos)
                track.translation.append((v[0], v[1:4]))
            for _ in range(nr):
                v, pos = r.unpack(F_KEY_R, pos)
                track.rotation.append((v[0], v[1:5]))
            for _ in range(ns):
                v, pos = r.unpack(F_KEY_S, pos)
                track.scale.append((v[0], v[1]))
            for series in (track.translation, track.rotation, track.scale):
                times = [t for t, _v in series]
                if any(b <= a for a, b in zip(times, times[1:])):
                    raise NmoError('clip %r has unsorted key times' % name)
            total += track.key_count()
            clip.tracks.append(track)
        if total != key_count:
            raise NmoError('clip %r declares %d keys, tracks hold %d' % (name, key_count, total))
    else:
        raise NmoError('clip %r uses unknown key encoding %d' % (name, encoding))
    return clip, pos


def _read_marker(r, pos):
    name, pos = r.string(pos)
    v, pos = r.unpack(F_MARKER, pos)
    return Marker(name=name, position=v[0:3], orientation=v[3:7], scale=v[7],
                  parent_bone=v[8], flags=v[9]), pos


def _read_buffers(r, offset, count, kind):
    buffers = []
    pos = r.at(offset) if count else 0
    for _ in range(count):
        pos = _align(pos - r.base, BUFFER_ALIGN) + r.base
        (fmt, stride, elements, _res), pos = r.unpack(F_BUFFER_HEADER, pos)
        table = {'index': INDEX_STRIDE, 'vertex': VERTEX_STRIDE, 'skin': SKIN_STRIDE}[kind]
        if fmt not in table:
            raise NmoError('%s buffer declares unknown format %d' % (kind, fmt))
        if stride != table[fmt]:
            raise NmoError('%s buffer stride %d does not match format %d' % (kind, stride, fmt))
        end = r.need(pos, stride * elements)
        if kind == 'index':
            code = '<%d%s' % (elements, 'H' if fmt == INDEX_U16 else 'I')
            buffers.append((fmt, list(struct.unpack_from(code, r.data, pos))))
        elif kind == 'vertex':
            out = []
            for i in range(elements):
                v = struct.unpack_from(F_VERTEX, r.data, pos + i * stride)
                out.append((v[0:3], v[3:6], v[6:10], v[10], v[11:13]))
            buffers.append(out)
        else:
            out = []
            for i in range(elements):
                v = struct.unpack_from(F_SKIN_VERTEX, r.data, pos + i * stride)
                out.append((v[0:4], v[4:8]))
            buffers.append(out)
        pos = _align(end - r.base, BUFFER_ALIGN) + r.base
    return buffers


def _validate_submesh(mesh, sub, where):
    if sub.material_index >= len(mesh.materials):
        raise NmoError('%s: materialIndex %d out of range' % (where, sub.material_index))
    if sub.index_buffer_index >= len(mesh.index_buffers):
        raise NmoError('%s: indexBufferIndex %d out of range' % (where, sub.index_buffer_index))
    if sub.vertex_buffer_index >= len(mesh.vertex_buffers):
        raise NmoError('%s: vertexBufferIndex %d out of range' % (where, sub.vertex_buffer_index))
    indices = mesh.index_buffers[sub.index_buffer_index][1]
    vertices = mesh.vertex_buffers[sub.vertex_buffer_index]
    last = sub.start_index + 3 * sub.primitive_count
    if last > len(indices):
        raise NmoError('%s: index range [%d,%d) exceeds its buffer (%d)'
                       % (where, sub.start_index, last, len(indices)))
    if sub.min_vertex < sub.base_vertex:
        raise NmoError('%s: minVertex %d below baseVertex %d' % (where, sub.min_vertex, sub.base_vertex))
    lo, hi = sub.min_vertex, sub.min_vertex + sub.vertex_count
    if hi > len(vertices):
        raise NmoError('%s: vertex window [%d,%d) exceeds its buffer (%d)' % (where, lo, hi, len(vertices)))
    for i in indices[sub.start_index:last]:
        biased = i + sub.base_vertex
        if not (lo <= biased < hi):
            raise NmoError('%s: index %d (biased %d) outside [%d,%d)' % (where, i, biased, lo, hi))
    scope = len(sub.bones) if sub.bones else len(mesh.bones)
    for bone_index, bone in enumerate(sub.bones):
        if bone.parent_index != NO_PARENT and not (0 <= bone.parent_index < bone_index):
            raise NmoError('%s: bone %r parent %d is not an earlier bone' % (where, bone.name, bone.parent_index))
        if bone.mesh_bone_index != NO_BONE and not (0 <= bone.mesh_bone_index < len(mesh.bones)):
            raise NmoError('%s: bone %r aliases mesh bone %d, which does not exist'
                           % (where, bone.name, bone.mesh_bone_index))
    for clip in sub.clips:
        for bone_index in _clip_bone_indices(clip):
            if not (0 <= bone_index < scope):
                raise NmoError('%s: clip %r keys bone %d, outside its scope of %d'
                               % (where, clip.name, bone_index, scope))
    for marker in sub.markers:
        if marker.parent_bone != NO_BONE and not (0 <= marker.parent_bone < scope):
            raise NmoError('%s: marker %r binds bone %d, outside its scope of %d'
                           % (where, marker.name, marker.parent_bone, scope))
    names = [m.name for m in sub.markers]
    if len(set(names)) != len(names):
        raise NmoError('%s: duplicate marker names' % where)
    if sub.facets and len(sub.facets) != sub.primitive_count:
        raise NmoError('%s: %d facet ids for %d triangles' % (where, len(sub.facets), sub.primitive_count))
    if sub.vertex_buffer_index < len(mesh.skin_buffers):
        skin = mesh.skin_buffers[sub.vertex_buffer_index]
        if skin:
            if scope == 0:
                raise NmoError('%s: skinned but has no bone scope' % where)
            for i in range(lo, hi):
                for bone_index, weight in zip(skin[i][0], skin[i][1]):
                    if weight and not (0 <= bone_index < scope):
                        raise NmoError('%s: skin vertex %d references bone %d, outside its scope of %d'
                                       % (where, i, bone_index, scope))


def _clip_bone_indices(clip):
    if clip.encoding == CLIP_SRT_TRACKS:
        return [t.bone_index for t in clip.tracks]
    return [k[0] for k in clip.keyframes]


def read_nmo(data, verify_crc=True):
    """Parse .nmo bytes into a Model, validating in specification order."""
    if len(data) < SIZE_FILE_HEADER:
        raise NmoError('file is shorter than its header')
    magic, major, minor, header_bytes, file_bytes, mesh_count, flags, crc, _res = \
        struct.unpack_from(F_FILE_HEADER, data, 0)
    if magic != FILE_MAGIC:
        raise NmoError('not an NMO file (magic 0x%08X)' % magic)
    if major != VERSION_MAJOR:
        raise NmoError('unsupported major version %d' % major)
    if header_bytes != SIZE_FILE_HEADER:
        raise NmoError('headerBytes is %d, expected %d' % (header_bytes, SIZE_FILE_HEADER))
    if file_bytes != len(data):
        raise NmoError('fileBytes is %d, actual size is %d' % (file_bytes, len(data)))
    directory_end = SIZE_FILE_HEADER + SIZE_MESH_REF * mesh_count
    if directory_end > file_bytes:
        raise NmoError('mesh directory of %d entries does not fit the file' % mesh_count)
    if verify_crc and crc:
        actual = zlib.crc32(data[SIZE_FILE_HEADER:]) & 0xFFFFFFFF
        if actual != crc:
            raise NmoError('payload CRC mismatch: header 0x%08X, actual 0x%08X' % (crc, actual))

    model = Model(flags=flags, version_minor=minor)
    for i in range(mesh_count):
        offset, length = struct.unpack_from(F_MESH_REF, data, SIZE_FILE_HEADER + i * SIZE_MESH_REF)
        if offset < directory_end or offset + length > file_bytes:
            raise NmoError('mesh %d window [%d,%d) is outside the file' % (i, offset, offset + length))
        model.meshes.append(_read_mesh(data, offset, offset + length, i))
    return model


def _read_mesh(data, base, limit, index):
    r = _Reader(data, base, limit)
    fields, _ = r.unpack(F_MESH_HEADER, base)
    (flags, name_offset, material_count, materials_offset, sub_count, sub_offset,
     ib_count, ib_offset, vb_count, vb_offset, skin_count, skin_offset,
     extents_offset, bone_count, bones_offset, clip_count, clips_offset) = fields[:17]

    mesh = Mesh(flags=flags)
    if name_offset:
        mesh.name, _ = r.string(r.at(name_offset))

    pos = r.at(materials_offset) if material_count else 0
    for _ in range(material_count):
        mat, pos = _read_material(r, pos)
        mesh.materials.append(mat)

    mesh.index_buffers = _read_buffers(r, ib_offset, ib_count, 'index')
    mesh.vertex_buffers = _read_buffers(r, vb_offset, vb_count, 'vertex')
    if skin_count and skin_count != vb_count:
        raise NmoError('mesh %d has %d skin buffers for %d vertex buffers' % (index, skin_count, vb_count))
    mesh.skin_buffers = _read_buffers(r, skin_offset, skin_count, 'skin')
    for i, skin in enumerate(mesh.skin_buffers):
        if skin and len(skin) != len(mesh.vertex_buffers[i]):
            raise NmoError('mesh %d skin buffer %d has %d entries, its vertex buffer has %d'
                           % (index, i, len(skin), len(mesh.vertex_buffers[i])))

    if extents_offset:
        values, _ = r.unpack(F_EXTENTS, r.at(extents_offset))
        mesh.extents = Extents(values[0:3], values[3], values[4:7], values[7:10])

    pos = r.at(bones_offset) if bone_count else 0
    for i in range(bone_count):
        bone, pos = _read_bone(r, pos)
        if bone.parent_index != NO_PARENT and not (0 <= bone.parent_index < i):
            raise NmoError('mesh %d bone %r parent %d is not an earlier bone'
                           % (index, bone.name, bone.parent_index))
        if bone.mesh_bone_index != NO_BONE:
            raise NmoError('mesh %d bone %r carries an alias; mesh bones must not'
                           % (index, bone.name))
        mesh.bones.append(bone)

    pos = r.at(clips_offset) if clip_count else 0
    for _ in range(clip_count):
        clip, pos = _read_clip(r, pos)
        for bone_index in _clip_bone_indices(clip):
            if not (0 <= bone_index < len(mesh.bones)):
                raise NmoError('mesh %d clip %r keys bone %d, outside the mesh skeleton'
                               % (index, clip.name, bone_index))
        mesh.clips.append(clip)

    pos = r.at(sub_offset) if sub_count else 0
    for s in range(sub_count):
        values, pos = r.unpack(F_SUBMESH, pos)
        sub = SubMesh(material_index=values[0], index_buffer_index=values[1],
                      vertex_buffer_index=values[2], start_index=values[3],
                      primitive_count=values[4], base_vertex=values[5],
                      min_vertex=values[6], vertex_count=values[7], flags=values[8])
        name_off = values[9]
        bone_count_s, bones_off, clip_count_s, clips_off, marker_count, markers_off, facets_off = values[10:17]
        sub.extents = Extents(values[17:20], values[20], values[21:24], values[24:27])
        if name_off:
            sub.name, _ = r.string(r.at(name_off))
        p = r.at(bones_off) if bone_count_s else 0
        for _ in range(bone_count_s):
            bone, p = _read_bone(r, p)
            sub.bones.append(bone)
        p = r.at(clips_off) if clip_count_s else 0
        for _ in range(clip_count_s):
            clip, p = _read_clip(r, p)
            sub.clips.append(clip)
        p = r.at(markers_off) if marker_count else 0
        for _ in range(marker_count):
            marker, p = _read_marker(r, p)
            sub.markers.append(marker)
        if facets_off:
            p = r.at(facets_off)
            r.need(p, 4 * sub.primitive_count)
            sub.facets = list(struct.unpack_from('<%dI' % sub.primitive_count, data, p))
        _validate_submesh(mesh, sub, 'mesh %d submesh %d' % (index, s))
        mesh.sub_meshes.append(sub)
    return mesh
