#!/usr/bin/env python3
"""Exercise the Blender add-on headlessly: import an .nmo, export it, re-read.

Needs Blender's Python.  Either run it with Blender itself

    blender --background --python tools/nmo_blender_test.py

or with the `bpy` PyPI wheel in a virtualenv

    python -m venv env && env/bin/pip install bpy
    env/bin/python tools/nmo_blender_test.py

The model it builds is the shared fixture from nmo_fixture.py, so every feature of
the format crosses the Blender boundary: two submeshes, an aliased bone
palette, a local skeleton with an SRT clip, matrix keys at mesh scope, markers
both rigid and bone-bound, per-face facet ids, and the texture-atlas material.
"""

import importlib.util
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'blender_nmo'))

try:
    import bpy
except ImportError:
    sys.exit('this test needs Blender: run it under `blender --background --python`, '
             'or install the bpy wheel (see the docstring)')

import nmo_format as nmo  # noqa: E402
from nmo_fixture import build_model  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def load_addon():
    spec = importlib.util.spec_from_file_location(
        'blender_nmo', os.path.join(HERE, 'blender_nmo', '__init__.py'),
        submodule_search_locations=[os.path.join(HERE, 'blender_nmo')])
    addon = importlib.util.module_from_spec(spec)
    sys.modules['blender_nmo'] = addon
    spec.loader.exec_module(addon)
    addon.register()
    return addon


addon = load_addon()
model = build_model()
work = tempfile.mkdtemp(prefix='nmo-blender-')
source_path = os.path.join(work, 'source.nmo')
export_path = os.path.join(work, 'exported.nmo')
with open(source_path, 'wb') as handle:
    handle.write(nmo.write_nmo(model))

bpy.ops.wm.read_factory_settings(use_empty=True)
created = addon.import_nmo.load(bpy.context, source_path)
check(len(created) == 2, 'expected two submesh objects, imported %d' % len(created))
check({o.name for o in created} == {'Body', 'Dish'}, 'submesh names did not survive import')

armatures = {o.name: o for o in bpy.data.objects if o.type == 'ARMATURE'}
# Body's bone table is nothing but an alias, so it imports as a palette
# property rather than a second armature; only Dish has a real skeleton.
check(len(armatures) == 2, 'expected the mesh skeleton and Dish\'s, got %d: %s'
      % (len(armatures), sorted(armatures)))
body_object = bpy.data.objects.get('Body')
check(body_object is not None and list(body_object.get('nmo_palette', [])) == ['Root'],
      'the pure-alias bone table did not import as a palette')
dish_skeleton = armatures.get('Dish.Skeleton')
check(dish_skeleton is not None, 'the submesh skeleton was not created')
if dish_skeleton:
    check([b.name for b in dish_skeleton.data.bones] == ['HullTop', 'DishPivot'],
          'submesh bone order changed')
    check(dish_skeleton.data.bones['HullTop'].get('nmo_alias') == 'Root',
          'the bone alias was lost')

empties = {o.name: o for o in bpy.data.objects if o.type == 'EMPTY'}
check(set(empties) == {'TurretMount', 'Connector00', 'Muzzle0'}, 'markers did not become empties')
if 'Muzzle0' in empties:
    check(empties['Muzzle0'].parent_bone == 'DishPivot', 'a bound marker lost its bone')
if 'TurretMount' in empties:
    # NMO (0, 20, 0) is Y up; Blender is Z up.
    check(tuple(round(v, 4) for v in empties['TurretMount'].location) == (0.0, 0.0, 20.0),
          'marker position was not converted to Blender axes')

if body_object:
    check('nmo_facet' in body_object.data.attributes, 'facet ids did not reach the mesh')
    check(len(body_object.data.uv_layers) == 1, 'the UV layer is missing')

result = addon.export_nmo.save(bpy.context, export_path)
back = nmo.read_nmo(open(export_path, 'rb').read())
check(len(back.meshes) == 1, 'export produced %d meshes' % len(back.meshes))
mesh = back.meshes[0]
check(mesh.name == 'RadarTower', 'mesh name changed on export')
check([s.name for s in mesh.sub_meshes] == ['Body', 'Dish'], 'submesh names changed on export')
for sub in mesh.sub_meshes:
    check(sub.primitive_count == 2, '%s exported %d triangles, expected 2' % (sub.name, sub.primitive_count))
    check(sub.vertex_count == 4, '%s exported %d vertices; welding should keep it at 4'
          % (sub.name, sub.vertex_count))
    check(len(sub.facets) == sub.primitive_count, '%s lost its facet ids' % sub.name)
body = mesh.sub_meshes[0]
check(len(body.bones) == 1 and body.bones[0].mesh_bone_index == 0,
      'the bone palette did not survive export')
dish = mesh.sub_meshes[1]
check(len(dish.bones) == 2, 'the submesh skeleton did not survive export')
check(dish.bones[0].mesh_bone_index == 0, 'the bone alias did not survive export')
check(len(dish.clips) == 1 and dish.clips[0].name == 'Sweep', 'the submesh clip did not survive export')
if dish.clips:
    check(dish.clips[0].encoding == nmo.CLIP_SRT_TRACKS, 'clips should export as SRT tracks')
    check(len(dish.clips[0].tracks) == 1, 'track count changed')
check(len(dish.markers) == 1 and dish.markers[0].parent_bone == 1,
      'the bound marker did not survive export')
names = {m.name for s in mesh.sub_meshes for m in s.markers}
check(names == {'TurretMount', 'Connector00', 'Muzzle0'}, 'markers changed on export: %s' % names)

print('blender %s: imported %d submeshes, exported %d bytes'
      % (bpy.app.version_string, len(created), os.path.getsize(export_path)))
if failures:
    for failure in failures:
        print('FAIL:', failure)
    sys.exit(1)
print('add-on import/export round-trip passed')
