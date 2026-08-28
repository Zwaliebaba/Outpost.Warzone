#!/usr/bin/env python3
"""Round-trip and rejection tests for the .nmo reference codec.

Builds a model exercising every feature the format has - two submeshes, an
aliased bone palette, a local turret skeleton, SRT and CMO-matrix clips,
bound and rigid markers, facet ids - writes it, reads it back and compares.
Then it corrupts the bytes in each way the specification says a loader must
reject, and checks that the loader does.

Run from the repository root:  python tools/nmo_roundtrip_test.py
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'blender_nmo'))
import nmo_format as nmo  # noqa: E402
from nmo_fixture import build_model  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def rejects(data, why):
    try:
        nmo.read_nmo(data)
    except nmo.NmoError:
        return
    failures.append('loader accepted a file it must reject: %s' % why)


def compare(a, b):
    check(len(a.meshes) == len(b.meshes), 'mesh count changed')
    for m0, m1 in zip(a.meshes, b.meshes):
        check(m0.name == m1.name, 'mesh name changed')
        check(len(m0.materials) == len(m1.materials), 'material count changed')
        for x, y in zip(m0.materials, m1.materials):
            check(x.name == y.name and x.textures == y.textures, 'material identity changed')
            check((x.render_flags, x.atlas_frame_count, x.atlas_tile_width, x.atlas_tile_height,
                   x.atlas_frames_per_row, x.atlas_selector) ==
                  (y.render_flags, y.atlas_frame_count, y.atlas_tile_width, y.atlas_tile_height,
                   y.atlas_frames_per_row, y.atlas_selector), 'material extension changed')
        check(m0.vertex_buffers == m1.vertex_buffers, 'vertex data changed')
        check(m0.index_buffers == m1.index_buffers, 'index data changed')
        check(m0.skin_buffers == m1.skin_buffers, 'skin data changed')
        check(len(m0.bones) == len(m1.bones), 'mesh bone count changed')
        check([c.name for c in m0.clips] == [c.name for c in m1.clips], 'mesh clip names changed')
        check(len(m0.sub_meshes) == len(m1.sub_meshes), 'submesh count changed')
        for s0, s1 in zip(m0.sub_meshes, m1.sub_meshes):
            check(s0.name == s1.name, 'submesh name changed')
            check((s0.material_index, s0.start_index, s0.primitive_count, s0.base_vertex,
                   s0.min_vertex, s0.vertex_count, s0.flags) ==
                  (s1.material_index, s1.start_index, s1.primitive_count, s1.base_vertex,
                   s1.min_vertex, s1.vertex_count, s1.flags), 'submesh draw range changed')
            check(s0.facets == s1.facets, 'facet ids changed')
            check([b.name for b in s0.bones] == [b.name for b in s1.bones], 'submesh bone names changed')
            check([b.mesh_bone_index for b in s0.bones] == [b.mesh_bone_index for b in s1.bones],
                  'submesh bone aliases changed')
            check([c.name for c in s0.clips] == [c.name for c in s1.clips], 'submesh clip names changed')
            for c0, c1 in zip(s0.clips, s1.clips):
                check(c0.encoding == c1.encoding, 'clip encoding changed')
                check(len(c0.tracks) == len(c1.tracks), 'track count changed')
                for t0, t1 in zip(c0.tracks, c1.tracks):
                    check(t0.bone_index == t1.bone_index, 'track bone changed')
                    check(len(t0.rotation) == len(t1.rotation), 'rotation key count changed')
            check([(m.name, m.parent_bone) for m in s0.markers] ==
                  [(m.name, m.parent_bone) for m in s1.markers], 'markers changed')
            for k0, k1 in zip(s0.markers, s1.markers):
                check(all(abs(p - q) < 1e-6 for p, q in zip(k0.position, k1.position)),
                      'marker position changed')
                check(all(abs(p - q) < 1e-6 for p, q in zip(k0.orientation, k1.orientation)),
                      'marker orientation changed')


model = build_model()
data = nmo.write_nmo(model)
compare(model, nmo.read_nmo(data))

check(len(data) % 16 == 0, 'file does not end on a 16-byte boundary')
first_ref = struct.unpack_from('<I', data, nmo.SIZE_FILE_HEADER)[0]
check(first_ref % 16 == 0, 'mesh blob is not 16-byte aligned')
check(nmo.write_nmo(nmo.read_nmo(data)) == data, 'write(read(x)) is not byte-identical to x')

# --- rejection cases, one per validation clause -------------------------
rejects(b'PIE 2\n' + data[6:], 'wrong magic')
rejects(data[:4] + struct.pack('<H', 2) + data[6:], 'unsupported major version')
rejects(data[:12] + struct.pack('<I', len(data) + 16) + data[16:], 'fileBytes disagrees with the file')
rejects(data[:len(data) // 2], 'truncated file')
bad = bytearray(data)
bad[nmo.SIZE_FILE_HEADER + 4:nmo.SIZE_FILE_HEADER + 8] = struct.pack('<I', len(data) * 4)
rejects(bytes(bad), 'mesh window past end of file')
bad = bytearray(data)
bad[nmo.SIZE_FILE_HEADER - 8:nmo.SIZE_FILE_HEADER - 4] = struct.pack('<I', 0xDEADBEEF)
rejects(bytes(bad), 'payload CRC mismatch')


def mutate_mesh(fn, why):
    m = build_model()
    fn(m.meshes[0])
    try:
        raw = nmo.write_nmo(m)
    except nmo.NmoError:
        return                      # the writer refused first, which is also correct
    rejects(raw, why)


mutate_mesh(lambda m: setattr(m.sub_meshes[0], 'primitive_count', 99), 'index range past its buffer')
mutate_mesh(lambda m: setattr(m.sub_meshes[1], 'min_vertex', 0) or
                      setattr(m.sub_meshes[1], 'vertex_count', 2), 'index outside the vertex window')
mutate_mesh(lambda m: setattr(m.sub_meshes[0], 'material_index', 7), 'materialIndex out of range')
mutate_mesh(lambda m: setattr(m.sub_meshes[1].bones[1], 'parent_index', 5), 'bone parent is not earlier')
mutate_mesh(lambda m: setattr(m.sub_meshes[1].bones[0], 'mesh_bone_index', 9), 'alias of a missing mesh bone')
mutate_mesh(lambda m: setattr(m.sub_meshes[1].clips[0].tracks[0], 'bone_index', 9), 'clip keys a bone outside scope')
mutate_mesh(lambda m: setattr(m.sub_meshes[1].markers[0], 'parent_bone', 9), 'marker binds a bone outside scope')
mutate_mesh(lambda m: setattr(m.sub_meshes[0], 'facets', [0]), 'facet count disagrees with primitiveCount')
mutate_mesh(lambda m: setattr(m.bones[0], 'mesh_bone_index', 0), 'mesh-scope bone carries an alias')
mutate_mesh(lambda m: m.sub_meshes[0].markers.append(nmo.Marker(name='TurretMount')), 'duplicate marker names')
mutate_mesh(lambda m: setattr(m.sub_meshes[1].clips[0].tracks[0], 'rotation',
                              [(4.0, (0, 0, 0, 1)), (0.0, (0, 0, 0, 1))]), 'unsorted key times')
mutate_mesh(lambda m: setattr(m.clips[0], 'keyframes',
                              [(0, 1.0, nmo.IDENTITY_4X4), (0, 0.0, nmo.IDENTITY_4X4)]),
            'unsorted matrix keyframes')
mutate_mesh(lambda m: setattr(m.materials[0], 'name', 'x' * (nmo.MAX_STRING_BYTES + 1)), 'oversize string')
mutate_mesh(lambda m: m.skin_buffers[0].pop(), 'skin buffer shorter than its vertex buffer')

# Corrupt a name to invalid UTF-8 and clear the CRC, so the string check is
# what rejects the file rather than the checksum in front of it.
bad = bytearray(data)
bad[data.find(b'Hull')] = 0xFF                           # invalid UTF-8 lead byte
bad[24:28] = struct.pack('<I', 0)                        # payloadCrc32 = not computed
rejects(bytes(bad), 'invalid UTF-8 in a string')

print('round-trip: %d bytes, %d mesh, %d submeshes'
      % (len(data), len(model.meshes), len(model.meshes[0].sub_meshes)))
if failures:
    for f in failures:
        print('FAIL:', f)
    sys.exit(1)
print('all round-trip and rejection checks passed')
