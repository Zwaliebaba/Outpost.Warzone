"""Conventions shared by the .nmo importer and exporter.

The format is a runtime format; Blender is a scene graph.  This module is the
one place that states how the two map, so import and export cannot drift:

    NMO mesh          -> a Collection
    NMO submesh       -> a Mesh Object inside it (named for its role)
    NMO material      -> a Material, its extension fields in custom properties
    NMO bone table    -> an Armature Object; an alias entry is a bone carrying
                         the NMO_ALIAS custom property
    NMO clip          -> an Action on the armature, keys on pose bones
    NMO marker        -> an Empty parented to the submesh (or to its bone)
    NMO facet ids     -> an INT attribute on the face domain, so the quads a
                         triangle-only format destroys can be rebuilt exactly

Axes: NMO is left-handed, +Y up, +Z forward; Blender is right-handed, +Z up,
+Y forward.  swap_yz() converts either way - it is its own inverse - and is
applied to every position, so a model imported and exported unchanged comes
back byte-identical.
"""

NMO_ALIAS = 'nmo_alias'                 # bone -> name of the mesh bone it stands for
NMO_MARKER = 'nmo_marker'               # empty -> True
NMO_MARKER_FLAGS = 'nmo_marker_flags'
NMO_SUBMESH = 'nmo_submesh'             # object -> True
NMO_SUBMESH_FLAGS = 'nmo_submesh_flags'
NMO_MESH_NAME = 'nmo_mesh_name'         # collection -> NMO mesh name
NMO_ORDER = 'nmo_order'                 # object/material -> original index
NMO_SHADER = 'nmo_shader'
NMO_TEXTURES = 'nmo_textures'
NMO_RENDER_FLAGS = 'nmo_render_flags'
NMO_ATLAS_FRAMES = 'nmo_atlas_frame_count'
NMO_ATLAS_W = 'nmo_atlas_tile_width'
NMO_ATLAS_H = 'nmo_atlas_tile_height'
NMO_ATLAS_PER_ROW = 'nmo_atlas_frames_per_row'
NMO_ATLAS_SELECTOR = 'nmo_atlas_selector'
NMO_ATLAS_MS = 'nmo_atlas_frame_ms'
NMO_CLIP_START = 'nmo_clip_start'
NMO_CLIP_END = 'nmo_clip_end'
NMO_CLIP_ENCODING = 'nmo_clip_encoding'
NMO_SCOPE = 'nmo_scope'                 # armature -> 'mesh' or a submesh name
NMO_PALETTE = 'nmo_palette'             # object -> mesh bones this submesh uses

FACET_ATTRIBUTE = 'nmo_facet'
UV_LAYER = 'UVMap'
COLOR_ATTRIBUTE = 'Color'


def swap_yz(v):
    """Convert a position between NMO (Y up) and Blender (Z up).

    Swapping the last two components is an involution, which is exactly what a
    round-trip needs: no handedness bookkeeping, no accumulated sign errors.
    """
    return (v[0], v[2], v[1])


def quat_to_blender(q):
    """NMO stores (x, y, z, w); Blender's Quaternion is (w, x, y, z).

    The axis swap of swap_yz() applies to the vector part.
    """
    x, y, z, w = q
    return (w, x, z, y)


def quat_to_nmo(q):
    w, x, y, z = q
    return (x, z, y, w)


# --- Action compatibility --------------------------------------------------
#
# Blender 4.4 replaced Action.fcurves with slotted actions (slot -> layer ->
# strip -> channelbag -> fcurves) and 5.0 removed the old attribute outright.
# These two helpers are the only place either shape is named.

def action_fcurves(action, id_type='OBJECT', slot_name='Slot'):
    """Return (fcurve collection, slot) to write keys into, on any 4.2+ Blender."""
    legacy = getattr(action, 'fcurves', None)
    if legacy is not None:
        return legacy, None
    slot = action.slots[0] if len(action.slots) else action.slots.new(id_type=id_type, name=slot_name)
    layer = action.layers[0] if len(action.layers) else action.layers.new('Layer')
    strip = layer.strips[0] if len(layer.strips) else layer.strips.new(type='KEYFRAME')
    return strip.channelbag(slot, ensure=True).fcurves, slot


def iter_fcurves(action):
    """Every fcurve in an action, whichever shape the action has."""
    legacy = getattr(action, 'fcurves', None)
    if legacy is not None:
        return list(legacy)
    curves = []
    for layer in action.layers:
        for strip in layer.strips:
            for channelbag in getattr(strip, 'channelbags', []):
                curves.extend(channelbag.fcurves)
    return curves


def assign_action(animated, action, slot):
    """Bind an action (and its slot, where slots exist) to an ID's animation data."""
    animated.animation_data.action = action
    if slot is not None and hasattr(animated.animation_data, 'action_slot'):
        animated.animation_data.action_slot = slot
