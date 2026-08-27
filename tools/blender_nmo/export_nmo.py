"""Export the scene, or a selection, as a Neuron Mesh Object (.nmo)."""

import os

import bpy
from bpy.props import BoolProperty, StringProperty
from bpy_extras.io_utils import ExportHelper
from mathutils import Matrix

from . import nmo_format as nmo
from .nmo_scene import (COLOR_ATTRIBUTE, FACET_ATTRIBUTE, NMO_ALIAS, NMO_ATLAS_FRAMES,
                        NMO_ATLAS_H, NMO_ATLAS_MS, NMO_ATLAS_PER_ROW, NMO_ATLAS_SELECTOR,
                        NMO_ATLAS_W, NMO_CLIP_ENCODING, NMO_CLIP_END, NMO_CLIP_START,
                        NMO_MARKER, NMO_MARKER_FLAGS, NMO_MESH_NAME, NMO_ORDER, NMO_PALETTE,
                        NMO_RENDER_FLAGS, NMO_SCOPE, NMO_SHADER, NMO_SUBMESH,
                        NMO_SUBMESH_FLAGS, NMO_TEXTURES, UV_LAYER, iter_fcurves,
                        quat_to_nmo, swap_yz)


class ExportError(Exception):
    """Something in the scene cannot be expressed in the format."""


def _row_major(matrix):
    """mathutils.Matrix -> the 16 floats of an XMFLOAT4X4 (row-major)."""
    t = matrix.transposed()
    return tuple(component for row in t for component in row)


def _order_of(item, fallback):
    value = item.get(NMO_ORDER)
    return value if isinstance(value, int) else fallback


def _material_from(blender_material, index):
    if blender_material is None:
        return nmo.Material(name='Default')
    textures = list(blender_material.get(NMO_TEXTURES, []))
    material = nmo.Material(
        name=blender_material.name,
        textures=[str(t) for t in textures],
        shader=str(blender_material.get(NMO_SHADER, '')),
        render_flags=int(blender_material.get(NMO_RENDER_FLAGS, 0)),
        atlas_frame_count=int(blender_material.get(NMO_ATLAS_FRAMES, 0)),
        atlas_tile_width=int(blender_material.get(NMO_ATLAS_W, 0)),
        atlas_tile_height=int(blender_material.get(NMO_ATLAS_H, 0)),
        atlas_frames_per_row=int(blender_material.get(NMO_ATLAS_PER_ROW, 0)),
        atlas_selector=int(blender_material.get(NMO_ATLAS_SELECTOR, nmo.ATLAS_NONE)),
        atlas_frame_ms=int(blender_material.get(NMO_ATLAS_MS, 0)))
    if NMO_RENDER_FLAGS not in blender_material.keys() and not blender_material.use_backface_culling:
        material.render_flags |= nmo.MAT_DOUBLE_SIDED
    principled = None
    if blender_material.use_nodes:
        principled = blender_material.node_tree.nodes.get('Principled BSDF')
    if principled:
        material.diffuse = tuple(principled.inputs['Base Color'].default_value)
    return material


def _bones_from(armature_object, mesh_bone_names):
    """An armature becomes a bone table; NMO_ALIAS turns an entry into an alias."""
    if armature_object is None:
        return []
    bones = list(armature_object.data.bones)
    order = {bone.name: i for i, bone in enumerate(bones)}
    for i, bone in enumerate(bones):
        if bone.parent is not None and order[bone.parent.name] >= i:
            raise ExportError('armature %r lists bone %r before its parent; the format '
                              'requires parents first' % (armature_object.name, bone.name))
    out = []
    for bone in bones:
        bind = bone.matrix_local
        alias = bone.get(NMO_ALIAS, '')
        mesh_bone_index = mesh_bone_names.index(alias) if alias in mesh_bone_names else nmo.NO_BONE
        out.append(nmo.Bone(
            name=bone.name,
            parent_index=order[bone.parent.name] if bone.parent else nmo.NO_PARENT,
            bind_pose=_row_major(bind),
            inv_bind_pose=_row_major(bind.inverted_safe()),
            local_transform=_row_major(bone.matrix_local if bone.parent is None
                                       else bone.parent.matrix_local.inverted_safe() @ bind),
            mesh_bone_index=mesh_bone_index))
    return out


def _palette_bones(palette, mesh_bones, mesh_bone_names, where):
    """Rebuild a pure-alias bone table from the palette property.

    The matrices are copied from the mesh bone the entry stands for, which is
    what makes the entry an alias rather than an independent bone.
    """
    bones = []
    for name in palette:
        name = str(name)
        if name not in mesh_bone_names:
            raise ExportError('%s: bone palette names %r, which the mesh skeleton '
                              'does not have' % (where, name))
        index = mesh_bone_names.index(name)
        source = mesh_bones[index]
        bones.append(nmo.Bone(name=source.name, parent_index=nmo.NO_PARENT,
                              bind_pose=source.bind_pose,
                              inv_bind_pose=source.inv_bind_pose,
                              local_transform=source.local_transform,
                              mesh_bone_index=index))
    return bones


def _clips_from(armature_object, bones):
    """Every Action that keys these bones becomes a clip, SRT-encoded."""
    if armature_object is None or not bones:
        return []
    index_of = {bone.name: i for i, bone in enumerate(bones)}
    fps = bpy.context.scene.render.fps
    clips = []
    actions = []
    if armature_object.animation_data:
        if armature_object.animation_data.action:
            actions.append(armature_object.animation_data.action)
        for strip_track in armature_object.animation_data.nla_tracks:
            for strip in strip_track.strips:
                if strip.action and strip.action not in actions:
                    actions.append(strip.action)
    for action in actions:
        tracks = {}
        for curve in iter_fcurves(action):
            path = curve.data_path
            if not path.startswith('pose.bones["'):
                continue
            name = path.split('"')[1]
            if name not in index_of:
                continue
            field = path.rsplit('.', 1)[-1]
            entry = tracks.setdefault(index_of[name], {'location': {}, 'rotation_quaternion': {}, 'scale': {}})
            if field not in entry:
                continue
            for point in curve.keyframe_points:
                frame = round(point.co[0], 5)
                entry[field].setdefault(frame, [0.0] * 4)[curve.array_index] = point.co[1]
        if not tracks:
            continue
        out_tracks = []
        for bone_index in sorted(tracks):
            data = tracks[bone_index]
            track = nmo.SrtTrack(bone_index)
            for frame in sorted(data['location']):
                v = data['location'][frame]
                track.translation.append((frame / fps, swap_yz(v[0:3])))
            for frame in sorted(data['rotation_quaternion']):
                v = data['rotation_quaternion'][frame]
                track.rotation.append((frame / fps, quat_to_nmo(v)))
            for frame in sorted(data['scale']):
                v = data['scale'][frame]
                track.scale.append((frame / fps, v[0]))
            out_tracks.append(track)
        start, end = action.frame_range
        clips.append(nmo.Clip(name=action.name,
                              start_seconds=float(action.get(NMO_CLIP_START, start / fps)),
                              end_seconds=float(action.get(NMO_CLIP_END, end / fps)),
                              encoding=nmo.CLIP_SRT_TRACKS, tracks=out_tracks))
    return clips


def _markers_from(objects, bones):
    index_of = {bone.name: i for i, bone in enumerate(bones)}
    markers = []
    for obj in objects:
        matrix = obj.matrix_local
        location, rotation, scale = matrix.decompose()
        parent_bone = nmo.NO_BONE
        if obj.parent_type == 'BONE' and obj.parent_bone in index_of:
            parent_bone = index_of[obj.parent_bone]
        markers.append(nmo.Marker(name=obj.name,
                                  position=swap_yz(location),
                                  orientation=quat_to_nmo(rotation),
                                  scale=float(max(scale)),
                                  parent_bone=parent_bone,
                                  flags=int(obj.get(NMO_MARKER_FLAGS, 0))))
    return markers


def _evaluate(obj, depsgraph, apply_modifiers):
    source = obj.evaluated_get(depsgraph) if apply_modifiers else obj
    mesh = source.to_mesh()
    mesh.calc_loop_triangles()
    return source, mesh


def _submesh_geometry(obj, mesh, vertex_base):
    """Triangle corners become vertices, welded where every attribute matches.

    Welding on the full attribute tuple - not on position alone - keeps UV and
    normal seams intact while still collapsing the interior of a smooth surface,
    which is what a vertex buffer wants.
    """
    uv = mesh.uv_layers.get(UV_LAYER) or (mesh.uv_layers[0] if mesh.uv_layers else None)
    colors = mesh.color_attributes.get(COLOR_ATTRIBUTE)
    facet_attribute = mesh.attributes.get(FACET_ATTRIBUTE)
    world = obj.matrix_world

    vertices, indices, facets = [], [], []
    seen = {}
    for triangle in mesh.loop_triangles:
        for corner in range(3):
            loop_index = triangle.loops[corner]
            vertex_index = triangle.vertices[corner]
            position = world @ mesh.vertices[vertex_index].co
            normal = (world.to_3x3() @ mesh.loops[loop_index].normal).normalized()
            if uv is not None:
                u, v = uv.uv[loop_index].vector
                texcoord = (u, 1.0 - v)
            else:
                texcoord = (0.0, 0.0)
            if colors is not None:
                r, g, b, a = colors.data[loop_index].color
            else:
                r = g = b = a = 1.0
            packed = (min(int(r * 255 + 0.5), 255)
                      | min(int(g * 255 + 0.5), 255) << 8
                      | min(int(b * 255 + 0.5), 255) << 16
                      | min(int(a * 255 + 0.5), 255) << 24)
            key = (tuple(round(c, 6) for c in swap_yz(position)),
                   tuple(round(c, 6) for c in swap_yz(normal)),
                   packed, (round(texcoord[0], 6), round(texcoord[1], 6)))
            index = seen.get(key)
            if index is None:
                index = len(vertices)
                seen[key] = index
                vertices.append((swap_yz(position), swap_yz(normal), (1.0, 0.0, 0.0, 1.0),
                                 packed, texcoord))
            indices.append(vertex_base + index)
        if facet_attribute is not None:
            facets.append(int(facet_attribute.data[triangle.polygon_index].value))
        else:
            facets.append(triangle.polygon_index)
    return vertices, indices, facets


def _collect(context, use_selection):
    """Group the mesh objects to export into NMO meshes, one per collection."""
    pool = context.selected_objects if use_selection else context.scene.objects
    groups = {}
    for obj in pool:
        if obj.type != 'MESH':
            continue
        collection = next((c for c in obj.users_collection), context.scene.collection)
        groups.setdefault(collection, []).append(obj)
    ordered = sorted(groups.items(), key=lambda item: _order_of(item[0], 0))
    return [(collection, sorted(objects, key=lambda o: _order_of(o, 0)))
            for collection, objects in ordered if objects]


def _armature_for(obj, collection, scope_name):
    for modifier in obj.modifiers:
        if modifier.type == 'ARMATURE' and modifier.object is not None:
            return modifier.object
    for candidate in collection.objects:
        if candidate.type == 'ARMATURE' and candidate.get(NMO_SCOPE) == scope_name:
            return candidate
    return None


def _mesh_armature(collection):
    for candidate in collection.objects:
        if candidate.type == 'ARMATURE' and candidate.get(NMO_SCOPE) == 'mesh':
            return candidate
    return None


def save(context, filepath, use_selection=False, apply_modifiers=True):
    depsgraph = context.evaluated_depsgraph_get()
    model = nmo.Model()

    for collection, objects in _collect(context, use_selection):
        mesh = nmo.Mesh(name=str(collection.get(NMO_MESH_NAME, collection.name)))
        mesh_armature = _mesh_armature(collection)
        mesh.bones = _bones_from(mesh_armature, [])
        mesh_bone_names = [b.name for b in mesh.bones]
        mesh.clips = _clips_from(mesh_armature, mesh.bones)

        vertices, indices = [], []
        material_index_of = {}
        empties = [o for o in collection.objects if o.type == 'EMPTY' and o.get(NMO_MARKER)]

        for obj in objects:
            source, evaluated = _evaluate(obj, depsgraph, apply_modifiers)
            try:
                blender_material = obj.material_slots[0].material if obj.material_slots else None
                key = blender_material.name if blender_material else ''
                if key not in material_index_of:
                    material_index_of[key] = len(mesh.materials)
                    mesh.materials.append(_material_from(blender_material, len(mesh.materials)))

                base = len(vertices)
                sub_vertices, sub_indices, facets = _submesh_geometry(obj, evaluated, 0)
                if not sub_vertices:
                    continue
                vertices.extend(sub_vertices)
                start_index = len(indices)
                indices.extend(i + base for i in sub_indices)

                armature = _armature_for(obj, collection, obj.name)
                bones = []
                clips = []
                palette = list(obj.get(NMO_PALETTE, []))
                if palette:
                    bones = _palette_bones(palette, mesh.bones, mesh_bone_names, obj.name)
                elif armature is not None and armature is not mesh_armature:
                    bones = _bones_from(armature, mesh_bone_names)
                    clips = _clips_from(armature, bones)
                scope_bones = bones if bones else mesh.bones

                sub = nmo.SubMesh(
                    name=obj.name,
                    material_index=material_index_of[key],
                    index_buffer_index=0, vertex_buffer_index=0,
                    start_index=start_index,
                    primitive_count=len(sub_indices) // 3,
                    base_vertex=0, min_vertex=base, vertex_count=len(sub_vertices),
                    flags=int(obj.get(NMO_SUBMESH_FLAGS, 0)),
                    bones=bones, clips=clips, facets=facets,
                    extents=nmo.Extents.from_points([v[0] for v in sub_vertices]))
                children = [e for e in empties if e.parent is obj
                            or (e.parent is armature and armature is not None
                                and armature is not mesh_armature)]
                sub.markers = _markers_from(children, scope_bones)
                mesh.sub_meshes.append(sub)
            finally:
                source.to_mesh_clear()

        if not mesh.sub_meshes:
            continue
        mesh.vertex_buffers.append(vertices)
        fmt = nmo.INDEX_U16 if len(vertices) <= 0xFFFF else nmo.INDEX_U32
        mesh.index_buffers.append((fmt, indices))
        mesh.extents = nmo.Extents.from_points([v[0] for v in vertices])
        model.meshes.append(mesh)

    if not model.meshes:
        raise ExportError('nothing to export: no mesh objects were found')

    data = nmo.write_nmo(model)
    nmo.read_nmo(data)                 # never write a file this codec would reject
    with open(filepath, 'wb') as handle:
        handle.write(data)
    return model


class ExportNmo(bpy.types.Operator, ExportHelper):
    """Save the scene as a Neuron Mesh Object file"""
    bl_idname = 'export_scene.nmo'
    bl_label = 'Export NMO'
    bl_options = {'REGISTER'}

    filename_ext = '.nmo'
    filter_glob: StringProperty(default='*.nmo', options={'HIDDEN'})
    use_selection: BoolProperty(
        name='Selection only',
        description='Export the selected objects rather than the whole scene',
        default=False)
    apply_modifiers: BoolProperty(
        name='Apply modifiers',
        description='Export the evaluated mesh, with modifiers applied',
        default=True)

    def execute(self, context):
        try:
            model = save(context, self.filepath,
                         use_selection=self.use_selection,
                         apply_modifiers=self.apply_modifiers)
        except (ExportError, nmo.NmoError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}
        submeshes = sum(len(m.sub_meshes) for m in model.meshes)
        self.report({'INFO'}, 'Exported %d meshes, %d submeshes' % (len(model.meshes), submeshes))
        return {'FINISHED'}
