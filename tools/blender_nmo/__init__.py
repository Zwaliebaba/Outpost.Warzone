"""Blender add-on: import and export Neuron Mesh Object (.nmo) models.

The format is specified in Docs/NeuronMeshObject.md; nmo_format.py in this
directory is its reference codec and the only module that touches bytes.

Installing:
  Blender 4.2 and newer treat this directory as an extension.  Zip it and use
  Edit > Preferences > Add-ons > Install from Disk, or symlink/copy the
  directory into the extensions user path.  For a repository checkout the
  quickest route is:

      blender --command extension build --source-dir tools/blender_nmo

  bl_info below keeps the legacy add-on installer working as well; Blender
  prefers blender_manifest.toml when both are present.

What round-trips:
  Meshes, submeshes with their draw ranges, materials with the texture-atlas
  extension, per-submesh and per-mesh skeletons (including bones that alias a
  mesh bone), SRT and matrix clips, markers, and the facet ids that let quads
  survive a trip through a triangle-only format.
"""

bl_info = {
    "name": "Neuron Mesh Object (.nmo)",
    "author": "Outpost: Warzone contributors",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "File > Import-Export",
    "description": "Import and export Neuron Mesh Object models",
    "category": "Import-Export",
}

import bpy

from . import import_nmo
from . import export_nmo

_CLASSES = (
    import_nmo.ImportNmo,
    export_nmo.ExportNmo,
)


def _import_menu(self, context):
    self.layout.operator(import_nmo.ImportNmo.bl_idname, text="Neuron Mesh Object (.nmo)")


def _export_menu(self, context):
    self.layout.operator(export_nmo.ExportNmo.bl_idname, text="Neuron Mesh Object (.nmo)")


def register():
    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_import.append(_import_menu)
    bpy.types.TOPBAR_MT_file_export.append(_export_menu)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(_export_menu)
    bpy.types.TOPBAR_MT_file_import.remove(_import_menu)
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
