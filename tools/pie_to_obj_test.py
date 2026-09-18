#!/usr/bin/env python3
"""Tests for the .pie -> .obj export.

Two halves.  The first builds a .pie by hand - a closed cube of quads, a
degenerate polygon, two levels - and checks the .obj that comes back: the axis
conversion, the winding, the triangulation, the level split, the vertex
numbering.  A cube is used because its answer is checkable by arithmetic: a
closed surface has a signed volume, and the sign says which way the faces face.

The second runs the real corpus and asserts the property that actually matters
and that no synthetic fixture can establish - that no closed model comes out
inside-out.  Docs/PieToNmoMigration.md §5.12 records winding as needing a
visual check; for the closed subset this is a better one.

Run from the repository root:  python tools/pie_to_obj_test.py
"""

import contextlib
import io
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pie_to_obj as exporter  # noqa: E402
from pie_format import find_pies, parse_pie  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


# A unit cube, 1 unit per side, at the origin corner. Quads wound so that -
# read as right-handed - they face outward, which is what the corpus does
# (see the second half of this file) and therefore what the export must assume.
CUBE_POINTS = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
               (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
CUBE_QUADS = [(0, 3, 2, 1),      # z = 0, facing -Z
              (4, 5, 6, 7),      # z = 1, facing +Z
              (0, 1, 5, 4),      # y = 0, facing -Y
              (3, 7, 6, 2),      # y = 1, facing +Y
              (0, 4, 7, 3),      # x = 0, facing -X
              (1, 2, 6, 5)]      # x = 1, facing +X


def write_pie(path, levels):
    """Write a minimal PIE 2 file: textured quads, one TEXTURE page, no BSP."""
    lines = ['PIE 2', 'TYPE 200', 'TEXTURE 0 page-0 test.pcx 256 256',
             'LEVELS %d' % len(levels)]
    for index, (points, polygons) in enumerate(levels):
        lines.append('LEVEL %d' % (index + 1))
        lines.append('POINTS %d' % len(points))
        lines.extend('\t%d %d %d' % p for p in points)
        lines.append('POLYGONS %d' % len(polygons))
        for polygon in polygons:
            # 0x200 is PIE_TEXTURED, so each corner carries a u v pair.
            lines.append('\t200 %d %s %s'
                         % (len(polygon), ' '.join(str(i) for i in polygon),
                            ' '.join('0 0' for _ in polygon)))
    with open(path, 'w', newline='\n') as handle:
        handle.write('\n'.join(lines) + '\n')


def read_obj(text):
    """Return (positions, faces) from .obj text, faces as 0-based triples."""
    positions = []
    faces = []
    for line in text.split('\n'):
        parts = line.split()
        if not parts or parts[0].startswith('#'):
            continue
        if parts[0] == 'v':
            positions.append(tuple(int(c) for c in parts[1:4]))
        elif parts[0] == 'f':
            check(not any('/' in p for p in parts[1:]),
                  'faces reference UVs or normals the export does not write')
            faces.append(tuple(int(p) - 1 for p in parts[1:]))
        else:
            check(parts[0] in ('o', 'g'), 'unexpected .obj directive %r' % parts[0])
    return positions, faces


directory = tempfile.mkdtemp()

# --- one level: a cube, plus a quad with a repeated corner -------------------
degenerate = (0, 1, 1, 2)
single = os.path.join(directory, 'Cube.PIE')
write_pie(single, [(CUBE_POINTS, CUBE_QUADS + [degenerate])])
pie = parse_pie(single)
outputs, warnings = exporter.export(pie)

check(len(outputs) == 1, 'a single-level .pie produced %d files' % len(outputs))
positions, faces = read_obj(outputs[0][1])

check(positions == [exporter.position(p) for p in CUBE_POINTS],
      'positions are not the PIE points with Z negated')
check(len(positions) == len(CUBE_POINTS),
      'vertex count changed, so an .obj index is no longer its PIE point index + 1')
check(all(len(f) == 3 for f in faces), 'a face is not a triangle')
# 6 quads -> 12 triangles; the degenerate quad contributes the one triangle of
# its fan that is not a line.
check(len(faces) == 13, 'expected 13 triangles from 6 quads and a degenerate one, got %d'
      % len(faces))
check(sum('degenerate' in w for w in warnings) == 1,
      'expected exactly one degenerate-triangle warning, got %r' % warnings)

volume = exporter.signed_volume(positions, faces)
check(exporter.closed([f for f in faces if len(set(f)) == 3][:12]),
      'the cube did not come out closed')
check(volume == 6, 'cube faces are inside-out or mis-scaled: 6x volume is %d' % volume)

# The winding claim as its own check: negating an axis is a reflection, which
# inverts the volume, so the conversion has to flip the index order to get the
# sign back. Reversing the export's own faces must therefore produce the
# inward-facing cube that negating Z alone would have given.
check(exporter.signed_volume(positions, [f[::-1] for f in faces]) == -6,
      'the winding check itself is wrong: reversing should invert the volume')

raw_outputs, _ = exporter.export(pie, raw=True)
raw_positions, raw_faces = read_obj(raw_outputs[0][1])
check(raw_positions == CUBE_POINTS, '--raw did not keep the PIE coordinates verbatim')
check(raw_faces == [f[::-1] for f in faces],
      '--raw did not keep the declared index order')
# Both readings are outward-facing, and that is the whole point of doing the two
# flips together: negating Z alone would invert the faces, reversing alone would
# invert them too. What the conversion changes is chirality - --raw is the
# mirror image of the true shape - which a volume cannot see, so the mirroring
# is not what this asserts.
check(exporter.signed_volume(raw_positions, raw_faces) == 6,
      '--raw should still read as outward-facing; only the chirality differs')

# --- two levels: one file each, named and numbered --------------------------
shifted = [(x, y + 10, z) for x, y, z in CUBE_POINTS]
multi = os.path.join(directory, 'Stack.PIE')
write_pie(multi, [(CUBE_POINTS, CUBE_QUADS), (shifted, CUBE_QUADS)])
pie = parse_pie(multi)
outputs, _ = exporter.export(pie)
check(len(outputs) == 2, 'a two-level .pie produced %d files' % len(outputs))
check('o Stack_level00' in outputs[0][1] and 'o Stack_level01' in outputs[1][1],
      'levels are not named Stack_levelNN')
check(exporter.obj_path(multi, directory, 1, 2) == 'Stack_level01.obj',
      'multi-level output path is %r' % exporter.obj_path(multi, directory, 1, 2))
check(exporter.obj_path(single, directory, 0, 1) == 'Cube.obj',
      'single-level output path is %r' % exporter.obj_path(single, directory, 0, 1))
check(read_obj(outputs[1][1])[0] == [exporter.position(p) for p in shifted],
      'level 1 did not get level 1 geometry')

# --- the mirrored tree, and that nothing is written under --report ----------
nested = os.path.join(directory, 'structs')
os.makedirs(nested, exist_ok=True)
write_pie(os.path.join(nested, 'Deep.pie'), [(CUBE_POINTS, CUBE_QUADS)])
out = os.path.join(directory, 'out')
with contextlib.redirect_stdout(io.StringIO()) as report:
    check(exporter.main(['--root', directory, '--out', out, '--report', directory]) == 0,
          '--report over the fixtures did not succeed')
tally = dict(line.rsplit(None, 1) for line in report.getvalue().split('\n')
             if line.startswith('  closed') or line.startswith('  open'))
check(tally.get('  closed inward') == '0' and tally.get('  closed outward') == '3',
      'the report orientation tally reads %r' % tally)
check(not os.path.exists(out), '--report wrote files')
with contextlib.redirect_stdout(io.StringIO()):
    check(exporter.main(['--root', directory, '--out', out, directory]) == 0,
          'exporting the fixtures did not succeed')
check(os.path.exists(os.path.join(out, 'structs', 'Deep.obj')),
      'the output tree does not mirror the input tree')
check(os.path.exists(os.path.join(out, 'Stack_level00.obj')) and
      os.path.exists(os.path.join(out, 'Stack_level01.obj')),
      'the multi-level files were not both written')

# --- the shipped corpus: no closed model may come out inside-out ------------
corpus = closed_levels = 0
inside_out = []
for path in find_pies('GameData'):
    pie = parse_pie(path)
    corpus += 1
    for index, level in enumerate(pie.levels):
        triangles = [t for _f, i, _u, _a in level.polygons
                     for t in exporter.triangulate(i) if len(set(t)) == 3]
        if not exporter.closed(triangles):
            continue
        closed_levels += 1
        if exporter.signed_volume([exporter.position(p) for p in level.points],
                                  triangles) < 0:
            inside_out.append('%s level %d' % (path, index))
check(corpus == 516, 'expected the 516 shipped models, walked %d' % corpus)
check(closed_levels > 0, 'no closed level found, so the winding check proved nothing')
check(not inside_out, 'closed models exported inside-out: %s' % ', '.join(inside_out))

print('fixtures: cube 6x volume %d, 2-level file split into %d, '
      '%d shipped models walked (%d closed, 0 inside-out)'
      % (volume, len(outputs), corpus, closed_levels))
if failures:
    for failure in failures:
        print('FAIL:', failure)
    sys.exit(1)
print('all .obj export checks passed')
