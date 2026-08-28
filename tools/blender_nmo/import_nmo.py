"""Import a Neuron Mesh Object (.nmo) into the current Blender scene."""

import os

import bpy
from bpy.props import BoolProperty, StringProperty
from bpy_extras.io_utils import ImportHelper
from mathutils import Matrix, Quaternion

from . import nmo_format as nmo
from .nmo_scene import (COLOR_ATTRIBUTE, FACET_ATTRIBUTE, NMO_ALIAS, NMO_ATLAS_FRAMES,
                        NMO_ATLAS_H, NMO_ATLAS_MS, NMO_ATLAS_PER_ROW, NMO_ATLAS_SELECTOR,
                        NMO_ATLAS_W, NMO_CLIP_ENCODING, NMO_CLIP_END, NMO_CLIP_START,
                        NMO_MARKER, NMO_MARKER_FLAGS, NMO_MESH_NAME, NMO_ORDER, NMO_PALETTE,
                        NMO_RENDER_FLAGS, NMO_SCOPE, NMO_SHADER, NMO_SUBMESH,
                        NMO_SUBMESH_FLAGS, NMO_TEXTURES, UV_LAYER, action_fcurves,
                        assign_action, quat_to_blender, swap_yz)


def _matrix_from_row_major(values):
    """NMO stores XMFLOAT4X4, which is row-major; mathutils.Matrix is row-of-rows."""
    return Matrix([values[0:4], values[4:8], values[8:12], values[12:16]]).transposed()


def _make_material(source, index):
    mat = bpy.data.materials.new(name=source.name or 'Material.%03d' % index)
    mat.use_nodes = True
    mat[NMO_ORDER] = index
    mat[NMO_SHADER] = source.shader
    mat[NMO_TEXTURES] = list(source.textures)
    mat[NMO_RENDER_FLAGS] = source.render_flags
    mat[NMO_ATLAS_FRAMES] = source.atlas_frame_count
    mat[NMO_ATLAS_W] = source.atlas_tile_width
    mat[NMO_ATLAS_H] = source.atlas_tile_height
    mat[NMO_ATLAS_PER_ROW] = source.atlas_frames_per_row
    mat[NMO_ATLAS_SELECTOR] = source.atlas_selector
    mat[NMO_ATLAS_MS] = source.atlas_frame_ms
    mat.use_backface_culling = not (source.render_flags & nmo.MAT_DOUBLE_SIDED)
    principled = mat.node_tree.nodes.get('Principled BSDF')
    if principled:
        principled.inputs['Base Color'].default_value = tuple(source.diffuse)
        emissive = source.emissive
        if 'Emission Color' in principled.inputs:
            principled.inputs['Emission Color'].default_value = tuple(emissive)
    return mat


def _build_mesh_data(name, mesh, sub, material):
    """One NMO submesh becomes one Blender mesh, with its own local vertices."""
    indices = mesh.index_buffers[sub.index_buffer_index][1]
    vertices = mesh.vertex_buffers[sub.vertex_buffer_index]
    lo = sub.min_vertex
    used = indices[sub.start_index:sub.start_index + 3 * sub.primitive_count]

    positions = [swap_yz(vertices[lo + i][0]) for i in range(sub.vertex_count)]
    triangles = [tuple(used[t * 3 + k] + sub.base_vertex - lo for k in range(3))
                 for t in range(sub.primitive_count)]

    data = bpy.data.meshes.new(name)
    data.from_pydata(positions, [], triangles)
    data.update()

    if data.polygons:
        # Create every layer before writing any of them: adding an attribute can
        # reallocate the domain, which invalidates references taken earlier.
        data.uv_layers.new(name=UV_LAYER)
        data.color_attributes.new(name=COLOR_ATTRIBUTE, type='BYTE_COLOR', domain='CORNER')
        if sub.facets:
            data.attributes.new(name=FACET_ATTRIBUTE, type='INT', domain='FACE')

        loop_vertices = [loop.vertex_index for loop in data.loops]
        flat_uv = []
        flat_color = []
        loop_normals = []
        for vertex_index in loop_vertices:
            source = vertices[lo + vertex_index]
            flat_uv.extend((source[4][0], 1.0 - source[4][1]))
            packed = source[3]
            flat_color.extend((((packed >> 0) & 0xFF) / 255.0,
                               ((packed >> 8) & 0xFF) / 255.0,
                               ((packed >> 16) & 0xFF) / 255.0,
                               ((packed >> 24) & 0xFF) / 255.0))
            loop_normals.append(swap_yz(source[1]))

        data.uv_layers[UV_LAYER].uv.foreach_set('vector', flat_uv)
        data.color_attributes[COLOR_ATTRIBUTE].data.foreach_set('color', flat_color)
        if sub.facets:
            facets = list(sub.facets[:len(data.polygons)])
            facets += [0] * (len(data.polygons) - len(facets))
            data.attributes[FACET_ATTRIBUTE].data.foreach_set('value', facets)

        data.shade_smooth()
        if any(any(component for component in n) for n in loop_normals):
            try:
                data.normals_split_custom_set(loop_normals)
            except (RuntimeError, ValueError):
                pass                  # degenerate normals: keep Blender's own
    if material:
        data.materials.append(material)
    data.update()
    return data


def _make_armature(context, collection, name, bones, mesh_bone_names, scope):
    if not bones:
        return None
    armature = bpy.data.armatures.new(name)
    obj = bpy.data.objects.new(name, armature)
    obj[NMO_SCOPE] = scope
    collection.objects.link(obj)

    context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode='EDIT')
    created = []
    for i, bone in enumerate(bones):
        edit_bone = armature.edit_bones.new(bone.name or 'Bone.%03d' % i)
        # Bind pose gives the bone its place; length is cosmetic, the matrix is
        # what the format actually carries.
        edit_bone.head = (0.0, 0.0, 0.0)
        edit_bone.tail = (0.0, 0.0, 1.0)
        created.append(edit_bone)
    for i, bone in enumerate(bones):
        if bone.parent_index != nmo.NO_PARENT:
            created[i].parent = created[bone.parent_index]
    for i, bone in enumerate(bones):
        created[i].matrix = _matrix_from_row_major(bone.bind_pose)
    bpy.ops.object.mode_set(mode='OBJECT')

    for i, bone in enumerate(bones):
        pose_bone = obj.pose.bones[i]
        pose_bone.rotation_mode = 'QUATERNION'
        if bone.mesh_bone_index != nmo.NO_BONE:
            alias = mesh_bone_names[bone.mesh_bone_index] if bone.mesh_bone_index < len(mesh_bone_names) else ''
            obj.data.bones[i][NMO_ALIAS] = alias
    return obj


def _apply_clips(armature_object, clips, bones):
    if not armature_object or not clips:
        return
    if armature_object.animation_data is None:
        armature_object.animation_data_create()
    for clip in clips:
        action = bpy.data.actions.new(name=clip.name or 'Clip')
        if hasattr(action, 'id_root'):
            action.id_root = 'OBJECT'          # legacy actions carry their own type
        curves, slot = action_fcurves(action, slot_name=armature_object.name)
        action[NMO_CLIP_START] = clip.start_seconds
        action[NMO_CLIP_END] = clip.end_seconds
        action[NMO_CLIP_ENCODING] = clip.encoding
        action.use_fake_user = True
        fps = bpy.context.scene.render.fps
        if clip.encoding == nmo.CLIP_SRT_TRACKS:
            for track in clip.tracks:
                if track.bone_index >= len(bones):
                    continue
                name = bones[track.bone_index].name
                _srt_curves(curves, name, track, fps)
        else:
            for bone_index, time, transform in clip.keyframes:
                if bone_index >= len(bones):
                    continue
                matrix = _matrix_from_row_major(transform)
                loc, rot, scale = matrix.decompose()
                name = bones[bone_index].name
                _matrix_curve(curves, name, time * fps, loc, rot, scale)
        assign_action(armature_object, action, slot)


def _fcurve(curves, path, index):
    for curve in curves:
        if curve.data_path == path and curve.array_index == index:
            return curve
    return curves.new(data_path=path, index=index)


def _key(curves, path, index, frame, value):
    curve = _fcurve(curves, path, index)
    curve.keyframe_points.insert(frame, value, options={'FAST'})


def _srt_curves(curves, bone_name, track, fps):
    base = 'pose.bones["%s"].' % bone_name
    for time, value in track.translation:
        for i, component in enumerate(swap_yz(value)):
            _key(curves, base + 'location', i, time * fps, component)
    for time, value in track.rotation:
        for i, component in enumerate(quat_to_blender(value)):
            _key(curves, base + 'rotation_quaternion', i, time * fps, component)
    for time, value in track.scale:
        for i in range(3):
            _key(curves, base + 'scale', i, time * fps, value)


def _matrix_curve(curves, bone_name, frame, loc, rot, scale):
    base = 'pose.bones["%s"].' % bone_name
    for i, component in enumerate(swap_yz(loc)):
        _key(curves, base + 'location', i, frame, component)
    for i, component in enumerate(rot):
        _key(curves, base + 'rotation_quaternion', i, frame, component)
    for i, component in enumerate(swap_yz(scale)):
        _key(curves, base + 'scale', i, frame, component)


def _make_markers(collection, sub, parent_object, armature_object, bones):
    for marker in sub.markers:
        empty = bpy.data.objects.new(marker.name or 'Marker', None)
        empty.empty_display_type = 'ARROWS'
        empty.empty_display_size = max(marker.scale, 0.01)
        empty[NMO_MARKER] = True
        empty[NMO_MARKER_FLAGS] = marker.flags
        empty.location = swap_yz(marker.position)
        empty.rotation_mode = 'QUATERNION'
        empty.rotation_quaternion = Quaternion(quat_to_blender(marker.orientation))
        collection.objects.link(empty)
        if marker.parent_bone != nmo.NO_BONE and armature_object and marker.parent_bone < len(bones):
            empty.parent = armature_object
            empty.parent_type = 'BONE'
            empty.parent_bone = bones[marker.parent_bone].name
        else:
            empty.parent = parent_object


def load(context, filepath, verify_crc=True):
    with open(filepath, 'rb') as handle:
        model = nmo.read_nmo(handle.read(), verify_crc=verify_crc)

    stem = os.path.splitext(os.path.basename(filepath))[0]
    scene_collection = context.scene.collection
    created = []

    for mesh_index, mesh in enumerate(model.meshes):
        name = mesh.name or ('%s.%03d' % (stem, mesh_index) if len(model.meshes) > 1 else stem)
        collection = bpy.data.collections.new(name)
        collection[NMO_MESH_NAME] = mesh.name
        collection[NMO_ORDER] = mesh_index
        scene_collection.children.link(collection)

        materials = [_make_material(m, i) for i, m in enumerate(mesh.materials)]
        mesh_bone_names = [b.name for b in mesh.bones]
        mesh_armature = _make_armature(context, collection, name + '.Skeleton',
                                       mesh.bones, mesh_bone_names, 'mesh')
        _apply_clips(mesh_armature, mesh.clips, mesh.bones)

        for sub_index, sub in enumerate(mesh.sub_meshes):
            sub_name = sub.name or '%s.%03d' % (name, sub_index)
            material = materials[sub.material_index] if materials else None
            data = _build_mesh_data(sub_name, mesh, sub, material)
            obj = bpy.data.objects.new(sub_name, data)
            obj[NMO_SUBMESH] = True
            obj[NMO_SUBMESH_FLAGS] = sub.flags
            obj[NMO_ORDER] = sub_index
            collection.objects.link(obj)

            armature = mesh_armature
            bones = mesh.bones
            if sub.bones and all(b.mesh_bone_index != nmo.NO_BONE for b in sub.bones):
                # Pure alias table: a palette naming which mesh bones this
                # submesh uses.  It needs no armature of its own - one would be
                # a second copy of the mesh skeleton for an artist to desync.
                obj[NMO_PALETTE] = [b.name for b in sub.bones]
                bones = sub.bones
                _apply_clips(mesh_armature, sub.clips, bones)
            elif sub.bones:
                armature = _make_armature(context, collection, sub_name + '.Skeleton',
                                          sub.bones, mesh_bone_names, sub_name)
                bones = sub.bones
                _apply_clips(armature, sub.clips, bones)
            elif sub.clips:
                _apply_clips(mesh_armature, sub.clips, mesh.bones)

            skin = mesh.skin_buffers[sub.vertex_buffer_index] if sub.vertex_buffer_index < len(mesh.skin_buffers) else []
            if skin and bones:
                groups = [obj.vertex_groups.new(name=b.name) for b in bones]
                for local in range(sub.vertex_count):
                    entry = skin[sub.min_vertex + local]
                    for bone_index, weight in zip(entry[0], entry[1]):
                        if weight > 0.0 and bone_index < len(groups):
                            groups[bone_index].add([local], weight, 'REPLACE')
                if armature:
                    modifier = obj.modifiers.new(name='Armature', type='ARMATURE')
                    modifier.object = armature
                    obj.parent = armature
            elif armature is not None and armature is not mesh_armature:
                obj.parent = armature

            _make_markers(collection, sub, obj, armature, bones)
            created.append(obj)

    context.view_layer.update()
    return created


class ImportNmo(bpy.types.Operator, ImportHelper):
    """Load a Neuron Mesh Object file"""
    bl_idname = 'import_scene.nmo'
    bl_label = 'Import NMO'
    bl_options = {'REGISTER', 'UNDO'}

    filename_ext = '.nmo'
    filter_glob: StringProperty(default='*.nmo', options={'HIDDEN'})
    verify_crc: BoolProperty(
        name='Verify checksum',
        description='Check the payload CRC recorded in the file header',
        default=True)

    def execute(self, context):
        try:
            created = load(context, self.filepath, verify_crc=self.verify_crc)
        except nmo.NmoError as error:
            self.report({'ERROR'}, 'Not a usable NMO file: %s' % error)
            return {'CANCELLED'}
        self.report({'INFO'}, 'Imported %d submeshes' % len(created))
        return {'FINISHED'}
