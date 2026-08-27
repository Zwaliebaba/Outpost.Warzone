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

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blender_nmo'))
import nmo_format as nmo  # noqa: E402

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


def quad(origin, size, first):
    """Two triangles and four vertices making an axis-aligned quad."""
    x, y, z = origin
    positions = [(x, y, z), (x + size, y, z), (x + size, y + size, z), (x, y + size, z)]
    verts = [(p, (0.0, 0.0, -1.0), (1.0, 0.0, 0.0, 1.0), 0xFFFFFFFF, (0.0, 0.0)) for p in positions]
    indices = [first, first + 1, first + 2, first, first + 2, first + 3]
    return verts, indices


def build_model():
    mesh = nmo.Mesh(name='RadarTower')
    mesh.materials.append(nmo.Material(name='Hull', textures=['page-12-player buildings.dds'],
                                       render_flags=nmo.MAT_ALPHA_TEST))
    mesh.materials.append(nmo.Material(name='DishTeam',
                                       textures=['page-12-player buildings.dds'],
                                       render_flags=nmo.MAT_DOUBLE_SIDED,
                                       atlas_frame_count=8, atlas_tile_width=32,
                                       atlas_tile_height=15, atlas_frames_per_row=8,
                                       atlas_selector=nmo.ATLAS_TEAM))

    body_v, body_i = quad((0.0, 0.0, 0.0), 10.0, 0)
    dish_v, dish_i = quad((0.0, 20.0, 0.0), 4.0, 4)
    mesh.vertex_buffers.append(body_v + dish_v)
    mesh.index_buffers.append((nmo.INDEX_U16, body_i + dish_i))
    mesh.skin_buffers.append([((0, 0, 0, 0), (1.0, 0.0, 0.0, 0.0)) for _ in range(8)])
    mesh.extents = nmo.Extents.from_points([v[0] for v in mesh.vertex_buffers[0]])

    # Mesh skeleton: one root the whole building hangs from.
    mesh.bones.append(nmo.Bone(name='Root'))
    mesh.clips.append(nmo.Clip(name='Idle', start_seconds=0.0, end_seconds=1.0,
                               encoding=nmo.CLIP_MATRIX_KEYS,
                               keyframes=[(0, 0.0, nmo.IDENTITY_4X4),
                                          (0, 1.0, nmo.IDENTITY_4X4)]))

    body = nmo.SubMesh(name='Body', material_index=0, index_buffer_index=0, vertex_buffer_index=0,
                       start_index=0, primitive_count=2, base_vertex=0,
                       min_vertex=0, vertex_count=4,
                       flags=nmo.SUB_DEFORMED_AT_RUNTIME,
                       facets=[0, 0],
                       extents=nmo.Extents.from_points([v[0] for v in body_v]),
                       markers=[nmo.Marker(name='TurretMount', position=(0.0, 20.0, 0.0)),
                                nmo.Marker(name='Connector00', position=(0.0, 21.0, 0.0))])
    # Aliased palette: the body's only bone stands for mesh bone 0.
    body.bones.append(nmo.Bone(name='Root', mesh_bone_index=0))

    # The dish carries its own skeleton and its own clip - requirement R-NMO-1.
    dish = nmo.SubMesh(name='Dish', material_index=1, index_buffer_index=0, vertex_buffer_index=0,
                       start_index=6, primitive_count=2, base_vertex=0,
                       min_vertex=4, vertex_count=4,
                       facets=[1, 1],
                       extents=nmo.Extents.from_points([v[0] for v in dish_v]))
    dish.bones.append(nmo.Bone(name='HullTop', mesh_bone_index=0))
    dish.bones.append(nmo.Bone(name='DishPivot', parent_index=0))
    dish.clips.append(nmo.Clip(name='Sweep', start_seconds=0.0, end_seconds=4.0,
                               tracks=[nmo.SrtTrack(1,
                                                    translation=[(0.0, (0.0, 0.0, 0.0))],
                                                    rotation=[(0.0, (0.0, 0.0, 0.0, 1.0)),
                                                              (2.0, (0.0, 1.0, 0.0, 0.0)),
                                                              (4.0, (0.0, 0.0, 0.0, 1.0))],
                                                    scale=[(0.0, 1.0)])]))
    dish.markers.append(nmo.Marker(name='Muzzle0', position=(0.0, 24.0, 0.0),
                                   orientation=(0.0, 0.0, 0.7071068, 0.7071068),
                                   parent_bone=1))
    mesh.sub_meshes += [body, dish]
    return nmo.Model(meshes=[mesh])


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
