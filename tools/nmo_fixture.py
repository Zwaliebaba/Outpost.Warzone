#!/usr/bin/env python3
"""The golden NMO model the tests are built on.

One fixture, three consumers: the Python codec test, the Blender add-on test
and tools/make_nmo_fixture.py, which turns it into a byte array for the C++
tests.  Keeping it in one place is what makes "the C++ loader and the Python
codec agree" a checkable claim rather than a hope.

It exercises every feature the format has: two submeshes over shared buffers,
a pure-alias bone palette, a local skeleton with an SRT clip, matrix keyframes
at mesh scope, markers both rigid and bone-bound, per-face facet ids, a
team-colour texture atlas, and a submesh flagged as deformed at runtime.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blender_nmo'))
import nmo_format as nmo  # noqa: E402


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
