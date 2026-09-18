#!/usr/bin/env python3
"""Export .pie (PIE 2) models as Wavefront .obj - geometry only, no materials.

A companion to tools/pie_to_nmo.py, not part of it: that converter produces the
runtime format the renderer will load, this one produces something Blender,
MeshLab or 3ds Max opens for inspection and editing.  Neither writes into
GameData.

  python tools/pie_to_obj.py                            # whole tree -> build/obj
  python tools/pie_to_obj.py --report                   # survey, write nothing
  python tools/pie_to_obj.py GameData/structs/BLDerik.PIE --out /tmp
  python tools/pie_to_obj.py --out build/obj --raw      # verbatim PIE coordinates

What the output does and does not contain:

*Geometry only.*  Positions and triangles, no `vt`, no `vn`, no `usemtl` and no
.mtl file.  The texture a .pie names is a page shared by dozens of models with
integer texel UVs into it, which is of no use without the page, so dropping it
keeps the export honest rather than shipping half a material.  PIE carries no
normals at all; an importer computing them from the faces is the same answer
this file could fabricate, minus the fabrication.

*Right-handed axes.*  PIE is left-handed, +X right, +Y up, +Z forward
(Docs/NeuronMeshObject.md, "Coordinates"); .obj consumers assume right-handed
with -Z forward.  The export negates Z and reverses each triangle, so the model
appears the right way round and the right way out.  --raw skips both and writes
the .pie numbers verbatim, which is mirrored in any right-handed viewer but
matches the file byte for byte when you are reading one by hand.

Reversing the winding is the half that is easy to get wrong, so it is measured
rather than assumed - Docs/PieToNmoMigration.md §5.12 leaves it open.  Read as
right-handed, the declared index order already gives an outward-facing
(positive signed volume) solid: 439 of the 465 levels with a non-zero volume,
and 10 of 10 that are closed and consistently oriented.  Negating an axis is a
reflection, which inverts that, so the winding has to flip back.  --report
prints the tally for the corpus you point it at.

*Zero-area triangles dropped, all 125 of them.*  pie_to_nmo.py reports 55,
and the two disagree for a reason worth knowing before diffing the reports: it
tests a whole polygon against vertices split by (point, u, v), so a quad whose
repeated corner carries two different UVs survives as two vertices at one
position.  Without UVs there is nothing to split by and nothing to keep - a
repeated point index means two corners of the triangle are the same place.  No
triangle with three distinct positions is dropped by either.

*One .obj per level.*  Ten files have several LEVELS, and nothing in the .pie
says whether they are animation frames or animated sub-objects
(tools/pie_format.py).  Either way stacking them in one file puts geometry on
top of itself, so each level becomes its own `<stem>_levelNN.obj`.
Single-level files - 506 of 516 - keep their plain name.

*Source names kept as they are.*  pie_to_nmo.py lowercases, because the stats
tables reference models by name and the game only survives three spellings of
one file by resolving case-insensitively.  Nothing references these .obj files,
so the useful property is the other one: an .obj traceable to its .pie at a
glance.  Two outputs could still want one path - a `Foo.pie` of two levels and a
`Foo_level00.pie` beside it, or a pair of names differing only in case on NTFS -
so the export folds case, checks, and fails rather than overwriting.  GameData
today has neither.

The default output is build/obj, which .gitignore already covers via its
`[Oo]bj/` rule - worth knowing before renaming it to something that isn't.
"""

import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pie_format import PieError, find_pies, parse_pie  # noqa: E402


def triangulate(indices, raw=False):
    """Fan-triangulate one PIE polygon: convex triangles and quads only.

    The fan cuts the same diagonal as pie_to_nmo.py, so the .obj and the .nmo
    of one model are the same surface rather than two readings of a non-planar
    quad.  Reversing each triangle - instead of reversing the corner list,
    which would cut the other diagonal - is what keeps that true.
    """
    for corner in range(1, len(indices) - 1):
        triangle = (indices[0], indices[corner], indices[corner + 1])
        yield triangle if raw else triangle[::-1]


def position(point, raw=False):
    """PIE (left-handed, +Z forward) -> .obj (right-handed, -Z forward).

    Points stay integers through the negation, so they are written as integers:
    exact, and readable next to the .pie they came from.
    """
    x, y, z = point
    return (x, y, z) if raw else (x, y, -z)


def level_to_obj(pie, level_index, name, raw=False):
    """Return (text, warnings) for one PIE level as a standalone .obj.

    Every point is emitted, in file order, so an .obj vertex index is its PIE
    point index plus one.  The corpus has no unreferenced points, so this costs
    nothing and buys a mesh whose numbering can be read against the source.
    """
    level = pie.levels[level_index]
    warnings = []
    lines = ['# Wavefront OBJ exported from %s by tools/pie_to_obj.py'
             % pie.path.replace(os.sep, '/'),
             '# PIE %d, level %d of %d. Geometry only: no materials, UVs or normals.'
             % (pie.version, level_index + 1, len(pie.levels))]
    if raw:
        lines.append('# Verbatim PIE coordinates: left-handed, +Y up, +Z forward.')
    else:
        lines.append('# Right-handed, +Y up, -Z forward: PIE\'s +Z negated and each')
        lines.append('# triangle reversed, so faces stay outward-facing.')
    lines.append('o %s' % name)
    for point in level.points:
        lines.append('v %d %d %d' % position(point, raw))

    faces = 0
    for _flags, indices, _uvs, _anim in level.polygons:
        for index in indices:
            if not 0 <= index < len(level.points):
                raise PieError('%s: polygon references point %d of %d'
                               % (pie.path, index, len(level.points)))
        for triangle in triangulate(indices, raw):
            if len(set(triangle)) < 3:
                # A repeated index means two corners are the same point, so
                # the triangle is a line. Authoring noise, 125 of it in the
                # shipped corpus (Docs/PieToNmoMigration.md §5.11) - not
                # something to hand a mesh editor.
                warnings.append('%s: dropped a degenerate triangle' % pie.path)
                continue
            lines.append('f %d %d %d' % tuple(i + 1 for i in triangle))
            faces += 1

    if not faces:
        warnings.append('%s: level has no usable triangles' % pie.path)
    return '\n'.join(lines) + '\n', warnings


def obj_path(pie_path, root, level_index, level_count):
    """Where one level's .obj goes: the input tree, mirrored.

    Mirroring rather than flattening matters for the same reason it does in
    pie_to_nmo.py - eight basenames in GameData repeat across directories.
    """
    relative = os.path.relpath(os.path.abspath(pie_path), os.path.abspath(root))
    if relative.startswith(os.pardir):
        relative = os.path.basename(pie_path)
    stem = os.path.splitext(relative)[0]
    if level_count > 1:
        stem = '%s_level%02d' % (stem, level_index)
    return stem.replace(os.sep, '/') + '.obj'


def signed_volume(points, triangles):
    """Six times the signed volume of a triangle soup, right-handed.

    Positive means the faces are counter-clockwise seen from outside, which is
    what .obj wants.  Only meaningful for a closed surface, so the report pairs
    it with closed() and keeps the two tallies apart: the negatives in
    GameData are billboards, jet flames and tree crosses, where the number is
    noise rather than a wrong winding.
    """
    total = 0
    for a, b, c in ((points[i], points[j], points[k]) for i, j, k in triangles):
        total += (a[0] * (b[1] * c[2] - b[2] * c[1])
                  - a[1] * (b[0] * c[2] - b[2] * c[0])
                  + a[2] * (b[0] * c[1] - b[1] * c[0]))
    return total


def closed(triangles):
    """True when every directed edge is matched by its reverse."""
    edges = collections.Counter()
    for i, j, k in triangles:
        edges[(i, j)] += 1
        edges[(j, k)] += 1
        edges[(k, i)] += 1
    return bool(edges) and all(edges.get((b, a), 0) == n for (a, b), n in edges.items())


def export(pie, raw=False):
    """Return ([(level index, .obj text)], warnings) for every level."""
    outputs = []
    warnings = []
    stem = os.path.splitext(os.path.basename(pie.path))[0]
    for index in range(len(pie.levels)):
        name = stem if len(pie.levels) == 1 else '%s_level%02d' % (stem, index)
        text, notes = level_to_obj(pie, index, name, raw)
        outputs.append((index, text))
        warnings.extend(notes)
    return outputs, warnings


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('inputs', nargs='*',
                        help='.pie files or directories (default: the whole --root)')
    parser.add_argument('--root', default='GameData',
                        help='tree the output mirrors (default: GameData)')
    parser.add_argument('--out', default=os.path.join('build', 'obj'),
                        help='directory to write the mirrored .obj tree into (default: build/obj)')
    parser.add_argument('--raw', action='store_true',
                        help='write verbatim PIE coordinates instead of converting to '
                             'right-handed axes; mirrored in any right-handed viewer')
    parser.add_argument('--report', action='store_true',
                        help='export in memory only and print what the corpus contains')
    args = parser.parse_args(argv)

    targets = []
    for item in args.inputs or [args.root]:
        targets.extend(find_pies(item) if os.path.isdir(item) else [item])

    totals = collections.Counter()
    warnings = collections.Counter()
    blocked = []
    collisions = []
    claimed = {}
    pie_bytes = obj_bytes = 0
    for path in targets:
        try:
            pie = parse_pie(path)
            outputs, notes = export(pie, args.raw)
        except PieError as error:
            blocked.append(str(error))
            totals['blocked'] += 1
            continue

        totals['exported'] += 1
        totals['levels'] += len(pie.levels)
        if len(pie.levels) > 1:
            totals['from multi-level sources'] += len(pie.levels)
        for note in notes:
            warnings[note.split(': ', 1)[-1]] += 1
        pie_bytes += os.path.getsize(path)

        for index, text in outputs:
            level = pie.levels[index]
            triangles = [t for _f, i, _u, _a in level.polygons
                         for t in triangulate(i, args.raw) if len(set(t)) == 3]
            totals['vertices'] += len(level.points)
            totals['triangles'] += len(triangles)
            volume = signed_volume([position(p, args.raw) for p in level.points], triangles)
            group = 'closed' if closed(triangles) else 'open'
            if volume > 0:
                totals['%s outward' % group] += 1
            elif volume < 0:
                totals['%s inward' % group] += 1
            else:
                totals['flat (no volume)'] += 1

            relative = obj_path(path, args.root, index, len(pie.levels))
            key = relative.lower()
            if key in claimed and claimed[key] != path:
                collisions.append((relative, claimed[key], path))
            claimed[key] = path
            obj_bytes += len(text.encode())

            if not args.report:
                destination = os.path.join(args.out, *relative.split('/'))
                os.makedirs(os.path.dirname(destination) or '.', exist_ok=True)
                with open(destination, 'w', newline='\n') as handle:
                    handle.write(text)

    print('input files          %d' % len(targets))
    print('exported             %d' % totals['exported'])
    print('blocked              %d' % totals['blocked'])
    print('output files         %d  (%d from multi-level sources)'
          % (len(claimed), totals['from multi-level sources']))
    print('vertices             %d' % totals['vertices'])
    print('triangles            %d' % totals['triangles'])
    print('axes                 %s'
          % ('verbatim PIE, left-handed' if args.raw else 'right-handed, -Z forward'))
    print('bytes  .pie %d  ->  .obj %d  (x%.2f)'
          % (pie_bytes, obj_bytes, (obj_bytes / pie_bytes) if pie_bytes else 0))
    if not args.report:
        print('written into         %s' % args.out)

    # Winding, measured rather than asserted (§5.12 of the migration doc). A
    # closed level facing inward is a real defect; an open one is a billboard.
    print('\nface orientation by signed volume:')
    for group in ('closed outward', 'closed inward', 'open outward', 'open inward',
                  'flat (no volume)'):
        print('  %-20s %d' % (group, totals[group]))

    if warnings:
        print('\nwarnings:')
        for note, count in warnings.most_common():
            print('  %-50s %d' % (note, count))
    if collisions:
        print('\noutput path collisions - two inputs, one output:')
        for relative, first, second in collisions:
            print('  %-40s %s and %s' % (relative, first, second))
    if blocked:
        print('\nblocked:')
        for reason in blocked:
            print('  %s' % reason)
    return 1 if (blocked or collisions or totals['closed inward']) else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BrokenPipeError:
        # Piping the report into head closes the pipe early; that is a normal
        # way to read it, not a crash, so do not print a traceback over it.
        sys.stderr.close()
        sys.exit(0)
