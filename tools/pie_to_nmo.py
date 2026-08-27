#!/usr/bin/env python3
"""Prototype converter: .pie (PIE 2) models to .nmo.

This exists to prove the migration plan in Docs/PieToNmoMigration.md is
executable and to measure it, not to be the production converter.  It converts
geometry, materials, connectors and - given the matching anims/*.json - the one
sub-object animation the corpus has.  What it cannot decide alone is listed by
--report, which is the point: the unresolved cases are the plan's work items.

  python tools/pie_to_nmo.py --report                 # survey, convert nothing
  python tools/pie_to_nmo.py --out build/models       # convert GameData
  python tools/pie_to_nmo.py GameData/structs/BLDerik.PIE --out /tmp

Level semantics are NOT in the .pie file: a multi-level file is a stack of
animation frames or a set of animated sub-objects depending on what the .ani
that references it says.  --anims points at the directory holding those, and
the converter refuses to guess when a multi-level file has no entry there.
"""

import argparse
import collections
import json
import math
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blender_nmo'))
import nmo_format as nmo  # noqa: E402

TEXTURE_LINE = re.compile(r'^\s*TEXTURE\s+(\d+)\s+(.*?)\s+(\d+)\s+(\d+)\s*$')

PIE_TEXTURED = 0x00000200
PIE_COLOURKEYED = 0x00000800
PIE_NO_CULL = 0x00002000
PIE_TEXANIM = 0x00004000
PIE_PSXTEX = 0x00008000
PIE_BSPFRESH = 0x00010000

# The .ani "frames" type swaps whole meshes per frame; "trans" animates one
# sub-object per level.  Nothing in the .pie says which it is.
LEVELS_ARE_FRAMES = 'frames'
LEVELS_ARE_OBJECTS = 'trans'


class PieError(Exception):
    pass


class Level:
    def __init__(self):
        self.points = []
        self.polygons = []
        self.connectors = []
        self.had_bsp = False


class Pie:
    def __init__(self, path):
        self.path = path
        self.version = 0
        self.type = 0
        self.texture = None
        self.texture_size = (256, 256)
        self.levels = []


def parse_pie(path):
    """Tokenise a PIE 2 file.

    Two shapes in the corpus defeat a naive split(): the TEXTURE line's file
    name contains spaces, and some files put a whole POLYGONS block on one
    tab-separated line.  Handling TEXTURE per line and everything else per
    token copes with both.
    """
    text = open(path, errors='replace').read().replace('\r\n', '\n').replace('\r', '\n')
    tokens = []
    pie = Pie(path)
    for line in text.split('\n'):
        match = TEXTURE_LINE.match(line)
        if match:
            pie.texture = match.group(2)
            pie.texture_size = (int(match.group(3)), int(match.group(4)))
            tokens.append('@TEXTURE')
        else:
            tokens.extend(line.split())

    position = 0

    def take():
        nonlocal position
        if position >= len(tokens):
            raise PieError('%s: file ends early' % path)
        value = tokens[position]
        position += 1
        return value

    def expect(word):
        value = take()
        if value != word:
            raise PieError('%s: expected %s, found %r' % (path, word, value))

    if tokens[position] == 'PIE':
        take()
        pie.version = int(take())
    expect('TYPE')
    pie.type = int(take(), 16)
    if position < len(tokens) and tokens[position] == '@TEXTURE':
        take()
    expect('LEVELS')
    level_count = int(take())

    for _ in range(level_count):
        expect('LEVEL')
        take()
        level = Level()
        expect('POINTS')
        for _ in range(int(take())):
            level.points.append((int(take()), int(take()), int(take())))
        expect('POLYGONS')
        for _ in range(int(take())):
            flags = int(take(), 16)
            corners = int(take())
            indices = [int(take()) for _ in range(corners)]
            anim = None
            if flags & PIE_TEXANIM:
                anim = tuple(int(take()) for _ in range(4))
            uvs = []
            if flags & (PIE_TEXTURED | PIE_PSXTEX):
                uvs = [(int(take()), int(take())) for _ in range(corners)]
            level.polygons.append((flags, indices, uvs, anim))
        while position < len(tokens) and tokens[position] != 'LEVEL':
            directive = take()
            if directive == 'CONNECTORS':
                for _ in range(int(take())):
                    level.connectors.append((int(take()), int(take()), int(take())))
            elif directive == 'BSP':
                # Runtime leftovers from a renderer deleted in Phase 8, and the
                # rows are variable width.  Skip to the next directive.
                level.had_bsp = True
                take()
                while position < len(tokens) and tokens[position] not in ('CONNECTORS', 'LEVEL'):
                    position += 1
            else:
                raise PieError('%s: unknown directive %r' % (path, directive))
        pie.levels.append(level)
    return pie


def load_anim_index(anims_dir):
    """basename of the .pie -> the anim json that drives it."""
    index = {}
    if not anims_dir or not os.path.isdir(anims_dir):
        return index
    for name in os.listdir(anims_dir):
        if not name.endswith('.json') or name == 'anim.json':
            continue
        try:
            data = json.load(open(os.path.join(anims_dir, name)))
        except (ValueError, OSError):
            continue
        if isinstance(data, dict) and 'pie' in data:
            index[str(data['pie']).lower()] = data
    return index


def _material_for(group_key, pie, index):
    flags, anim = group_key
    render_flags = 0
    if flags & PIE_COLOURKEYED:
        render_flags |= nmo.MAT_ALPHA_TEST
    if flags & PIE_NO_CULL:
        render_flags |= nmo.MAT_DOUBLE_SIDED
    texture = (pie.texture or '')
    material = nmo.Material(name='Material%d' % index,
                            textures=[texture],
                            render_flags=render_flags)
    if anim:
        frames, rate, tile_w, tile_h = anim
        # A negative frame count is the colour-key form; the magnitude is the
        # number of tiles either way.  Eight frames is the player count, which
        # is what the renderer means when it substitutes team for frame.
        count = abs(frames)
        material.atlas_frame_count = count
        material.atlas_tile_width = tile_w
        material.atlas_tile_height = tile_h
        material.atlas_frames_per_row = max(1, pie.texture_size[0] // tile_w) if tile_w else 0
        material.atlas_selector = nmo.ATLAS_TEAM if count == 8 else nmo.ATLAS_TIME
        material.atlas_frame_ms = 0 if count == 8 else max(rate, 1)
    return material


def _convert_level(pie, level, mesh, name, warnings):
    """One PIE level becomes one or more submeshes sharing the mesh's buffers."""
    width, height = pie.texture_size
    groups = collections.OrderedDict()
    for polygon_index, (flags, indices, uvs, anim) in enumerate(level.polygons):
        key = (flags & (PIE_COLOURKEYED | PIE_NO_CULL), anim)
        groups.setdefault(key, []).append((polygon_index, indices, uvs))

    vertices = mesh.vertex_buffers[0]
    index_buffer = mesh.index_buffers[0][1]
    created = []
    for key, polygons in groups.items():
        material = _material_for(key, pie, len(mesh.materials))
        material.name = '%s.%d' % (name, len(created))
        mesh.materials.append(material)

        first_index = len(index_buffer)
        first_vertex = len(vertices)
        cache = {}
        facets = []
        for polygon_index, indices, uvs in polygons:
            corners = []
            for corner, point_index in enumerate(indices):
                if point_index >= len(level.points):
                    raise PieError('%s: polygon references point %d of %d'
                                   % (pie.path, point_index, len(level.points)))
                u, v = uvs[corner] if uvs else (0, 0)
                cache_key = (point_index, u, v)
                slot = cache.get(cache_key)
                if slot is None:
                    position = level.points[point_index]
                    slot = len(vertices)
                    cache[cache_key] = slot
                    vertices.append((tuple(float(c) for c in position),
                                     (0.0, 0.0, 0.0),
                                     (1.0, 0.0, 0.0, 1.0),
                                     0xFFFFFFFF,
                                     (u / width, v / height)))
                corners.append(slot)
            if len(set(corners)) < 3:
                warnings.append('%s: dropped a degenerate polygon' % pie.path)
                continue
            # Fan-triangulate; PIE polygons are convex tris and quads only.
            for corner in range(1, len(corners) - 1):
                index_buffer.extend((corners[0], corners[corner], corners[corner + 1]))
                facets.append(polygon_index)

        count = len(vertices) - first_vertex
        if not count:
            mesh.materials.pop()
            continue
        _generate_normals(vertices, index_buffer, first_index, len(index_buffer))
        sub = nmo.SubMesh(name='%s.%d' % (name, len(created)) if len(groups) > 1 else name,
                          material_index=len(mesh.materials) - 1,
                          index_buffer_index=0, vertex_buffer_index=0,
                          start_index=first_index,
                          primitive_count=(len(index_buffer) - first_index) // 3,
                          base_vertex=0, min_vertex=first_vertex, vertex_count=count,
                          facets=facets,
                          extents=nmo.Extents.from_points(
                              [v[0] for v in vertices[first_vertex:]]))
        created.append(sub)
        mesh.sub_meshes.append(sub)
    return created


def _generate_normals(vertices, index_buffer, first, last):
    """PIE carries no normals; accumulate face normals so the data is complete."""
    sums = {}
    for i in range(first, last, 3):
        a, b, c = (vertices[index_buffer[i + k]][0] for k in range(3))
        ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        normal = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        for k in range(3):
            slot = index_buffer[i + k]
            acc = sums.setdefault(slot, [0.0, 0.0, 0.0])
            for axis in range(3):
                acc[axis] += normal[axis]
    for slot, acc in sums.items():
        length = math.sqrt(sum(c * c for c in acc))
        normal = tuple(c / length for c in acc) if length > 1e-9 else (0.0, 1.0, 0.0)
        position, _old, tangent, colour, uv = vertices[slot]
        vertices[slot] = (position, normal, tangent, colour, uv)


def _add_connectors(sub, level):
    """Connectors become named markers.

    They are stored (x, y, z) but consumed as a translation of (x, z, y): the
    third component is the height.  Points in the same file are already Y-up,
    so only connectors need the swap.
    """
    for i, (x, y, z) in enumerate(level.connectors):
        sub.markers.append(nmo.Marker(name='Connector%02d' % i,
                                      position=(float(x), float(z), float(y))))


def _track_from_anim(anim, level_index, bone_index):
    """One .ani sub-object's states become one SRT track on its bone.

    The .ani already stores position, rotation and scale per frame, so it maps
    onto SRT keys directly; baking it into 4x4 matrices first would lose
    rotation accuracy for nothing.
    """
    objects = anim.get('objects') or []
    entry = next((o for o in objects if int(o.get('index', -1)) == level_index), None)
    if entry is None:
        return None
    rate = max(int(anim.get('frameRate', 1)), 1)
    track = nmo.SrtTrack(bone_index)
    for state in entry['states']:
        frame = state[0]
        time = frame / float(rate)
        position = (state[1], state[2], state[3])
        rotation = (state[4], state[5], state[6])
        scale = (state[7], state[8], state[9])
        track.translation.append((time, tuple(float(c) for c in position)))
        track.rotation.append((time, _euler_to_quaternion(rotation)))
        track.scale.append((time, sum(scale) / 3000.0))
    return track if track.translation else None


def _euler_to_quaternion(rotation):
    """The .ani stores rotations in the engine's 1/1000 degree units."""
    half = [math.radians(c / 1000.0) * 0.5 for c in rotation]
    (sx, sy, sz) = [math.sin(h) for h in half]
    (cx, cy, cz) = [math.cos(h) for h in half]
    return (sx * cy * cz + cx * sy * sz,
            cx * sy * cz - sx * cy * sz,
            cx * cy * sz + sx * sy * cz,
            cx * cy * cz - sx * sy * sz)


def convert(pie, anim=None):
    """Return (Model, warnings).  Raises PieError when the plan needs a decision."""
    warnings = []
    stem = os.path.splitext(os.path.basename(pie.path))[0]
    if pie.levels and any(level.had_bsp for level in pie.levels):
        warnings.append('%s: dropped a BSP tree (dead since Phase 8)' % pie.path)

    kind = LEVELS_ARE_FRAMES
    if len(pie.levels) > 1:
        if anim is None:
            raise PieError('%s has %d levels and no animation to say what they are; '
                           'the migration must decide per file'
                           % (pie.path, len(pie.levels)))
        kind = anim.get('type', LEVELS_ARE_FRAMES)

    model = nmo.Model()
    if len(pie.levels) <= 1 or kind == LEVELS_ARE_OBJECTS:
        mesh = nmo.Mesh(name=stem)
        mesh.vertex_buffers.append([])
        mesh.index_buffers.append((nmo.INDEX_U16, []))
        objects = {int(o.get('index', i)): o.get('name', 'Object%d' % i)
                   for i, o in enumerate((anim or {}).get('objects') or [])}
        animated = anim is not None and kind == LEVELS_ARE_OBJECTS
        tracks = []
        for level_index, level in enumerate(pie.levels):
            name = objects.get(level_index, stem if len(pie.levels) == 1 else 'Object%d' % level_index)
            created = _convert_level(pie, level, mesh, name, warnings)
            if created:
                _add_connectors(created[0], level)
            if not animated or not created:
                continue
            # A level is the animated unit, but materials split it into several
            # submeshes.  Give the level one bone in the mesh skeleton and let
            # every submesh of that level alias it, so they all move together -
            # which is what the alias entry in a submesh bone table is for.
            bone_index = len(mesh.bones)
            mesh.bones.append(nmo.Bone(name=name))
            for sub in created:
                sub.bones.append(nmo.Bone(name=name, mesh_bone_index=bone_index))
            track = _track_from_anim(anim, level_index, bone_index)
            if track is not None:
                tracks.append(track)
        if tracks:
            end = max(t.translation[-1][0] for t in tracks if t.translation)
            mesh.clips.append(nmo.Clip(
                name=os.path.splitext(os.path.basename(str(anim.get('pie', stem))))[0],
                start_seconds=0.0, end_seconds=end,
                encoding=nmo.CLIP_SRT_TRACKS, tracks=tracks))
        _finish(mesh)
        model.meshes.append(mesh)
    else:
        for level_index, level in enumerate(pie.levels):
            mesh = nmo.Mesh(name='%s.frame%02d' % (stem, level_index))
            mesh.vertex_buffers.append([])
            mesh.index_buffers.append((nmo.INDEX_U16, []))
            created = _convert_level(pie, level, mesh, mesh.name, warnings)
            if created:
                _add_connectors(created[0], level)
            _finish(mesh)
            model.meshes.append(mesh)
    return model, warnings


def _finish(mesh):
    vertices = mesh.vertex_buffers[0]
    if len(vertices) > 0xFFFF:
        mesh.index_buffers[0] = (nmo.INDEX_U32, mesh.index_buffers[0][1])
    mesh.extents = nmo.Extents.from_points([v[0] for v in vertices])
    if not mesh.skin_buffers:
        mesh.skin_buffers.append([])


def find_pies(root):
    found = []
    for directory, _subdirs, files in os.walk(root):
        for name in files:
            if name.lower().endswith('.pie'):
                found.append(os.path.join(directory, name))
    return sorted(found)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('inputs', nargs='*', help='.pie files or directories (default: GameData)')
    parser.add_argument('--out', help='directory to write .nmo files into')
    parser.add_argument('--anims', default=os.path.join('GameData', 'anims'),
                        help='directory of animation json (default: GameData/anims)')
    parser.add_argument('--report', action='store_true',
                        help='convert in memory only and print what the corpus contains')
    args = parser.parse_args(argv)

    targets = []
    for item in args.inputs or [os.path.join('GameData')]:
        targets.extend(find_pies(item) if os.path.isdir(item) else [item])
    anims = load_anim_index(args.anims)

    totals = collections.Counter()
    warnings = collections.Counter()
    blocked = []
    pie_bytes = nmo_bytes = 0
    for path in targets:
        try:
            pie = parse_pie(path)
            anim = anims.get(os.path.basename(path).lower())
            model, notes = convert(pie, anim)
            data = nmo.write_nmo(model)
            nmo.read_nmo(data)                    # the converter validates its own output
        except (PieError, nmo.NmoError) as error:
            blocked.append((path, str(error)))
            totals['blocked'] += 1
            continue
        totals['converted'] += 1
        totals['meshes'] += len(model.meshes)
        totals['submeshes'] += sum(len(m.sub_meshes) for m in model.meshes)
        totals['markers'] += sum(len(s.markers) for m in model.meshes for s in m.sub_meshes)
        totals['clips'] += sum(len(m.clips) for m in model.meshes)
        totals['clips'] += sum(len(s.clips) for m in model.meshes for s in m.sub_meshes)
        totals['bones'] += sum(len(m.bones) for m in model.meshes)
        totals['aliased submeshes'] += sum(1 for m in model.meshes for s in m.sub_meshes if s.bones)
        totals['triangles'] += sum(s.primitive_count for m in model.meshes for s in m.sub_meshes)
        totals['vertices'] += sum(len(b) for m in model.meshes for b in m.vertex_buffers)
        totals['atlas_materials'] += sum(1 for m in model.meshes for mat in m.materials
                                         if mat.atlas_frame_count)
        for note in notes:
            warnings[note.split(': ', 1)[-1]] += 1
        pie_bytes += os.path.getsize(path)
        nmo_bytes += len(data)
        if args.out and not args.report:
            relative = os.path.splitext(os.path.basename(path))[0] + '.nmo'
            destination = os.path.join(args.out, relative)
            os.makedirs(args.out, exist_ok=True)
            with open(destination, 'wb') as handle:
                handle.write(data)

    print('input files          %d' % len(targets))
    print('converted            %d' % totals['converted'])
    print('blocked              %d' % totals['blocked'])
    print('meshes               %d' % totals['meshes'])
    print('submeshes            %d' % totals['submeshes'])
    print('triangles            %d' % totals['triangles'])
    print('vertices             %d' % totals['vertices'])
    print('markers              %d' % totals['markers'])
    print('clips                %d' % totals['clips'])
    print('animated bones       %d' % totals['bones'])
    print('submeshes w/ palette %d' % totals['aliased submeshes'])
    print('atlas materials      %d' % totals['atlas_materials'])
    print('bytes  .pie %d  ->  .nmo %d  (x%.2f)'
          % (pie_bytes, nmo_bytes, (nmo_bytes / pie_bytes) if pie_bytes else 0))
    if warnings:
        print('\nwarnings:')
        for note, count in warnings.most_common():
            print('  %-50s %d' % (note, count))
    if blocked:
        print('\nblocked - these need a decision from the migration plan:')
        for path, reason in blocked:
            print('  %s' % reason)
    return 1 if blocked else 0


if __name__ == '__main__':
    sys.exit(main())
