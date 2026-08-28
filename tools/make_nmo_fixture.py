#!/usr/bin/env python3
"""Generate NeuronClientTest/NmoFixture.h from the shared fixture.

The C++ loader and the Python codec have to agree byte for byte, and the only
way to keep that true is for both to be tested against the same bytes.  This
writes those bytes into a header the CppUnitTest project compiles, so no
binary asset is added to the tree and no test reads a file from disk.

Run from the repository root after changing the format or the fixture:

    python tools/make_nmo_fixture.py

It rewrites the header in place and reports whether anything changed.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'blender_nmo'))

import nmo_format as nmo  # noqa: E402
from nmo_fixture import build_model  # noqa: E402

TARGET = os.path.join(ROOT, 'NeuronClientTest', 'NmoFixture.h')

HEADER = '''#pragma once

/*
 * NmoFixture.h
 *
 * GENERATED - do not edit. Regenerate with:  python tools/make_nmo_fixture.py
 *
 * The golden .nmo, as bytes. It is written by tools/blender_nmo/nmo_format.py
 * - the reference codec - from the shared fixture in tools/nmo_fixture.py, so
 * a test that loads it is checking the C++ loader against the same file the
 * Python tests and the Blender add-on are checked against. Keeping it here
 * rather than in GameData/ also keeps a binary out of a directory whose
 * binaries are authored outside this repository.
 *
 * What it contains, and therefore what NmoTest.cpp can reach for:
{contents}
 */

#include <cstdint>

namespace NeuronClientTest
{{

inline constexpr std::uint8_t GoldenNmo[] = {{
{bytes}}};

inline constexpr std::size_t GoldenNmoBytes = sizeof(GoldenNmo);

}} // namespace NeuronClientTest
'''


def describe(model):
    lines = []
    for mesh in model.meshes:
        lines.append(' *   mesh %r: %d materials, %d submeshes, %d bones, %d clips'
                     % (mesh.name, len(mesh.materials), len(mesh.sub_meshes), len(mesh.bones), len(mesh.clips)))
        for sub in mesh.sub_meshes:
            lines.append(' *     submesh %r: %d triangles, %d vertices from %d, %d bones, %d clips, %d markers%s'
                         % (sub.name, sub.primitive_count, sub.vertex_count, sub.min_vertex,
                            len(sub.bones), len(sub.clips), len(sub.markers),
                            ', facet ids' if sub.facets else ''))
    return '\n'.join(lines)


def format_bytes(data, per_line=16):
    out = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        out.append('  ' + ' '.join('0x%02X,' % b for b in chunk))
    return '\n'.join(out) + '\n'


def main():
    model = build_model()
    data = nmo.write_nmo(model)
    nmo.read_nmo(data)                      # never generate a fixture the codec rejects
    text = HEADER.format(contents=describe(model), bytes=format_bytes(data))
    # Newlines are left to git: .gitattributes marks the tree text=auto, so a
    # regeneration that only changed line endings would be a diff every time.
    previous = open(TARGET, newline='').read() if os.path.exists(TARGET) else None
    if previous == text:
        print('%s is up to date (%d bytes of model)' % (os.path.relpath(TARGET, ROOT), len(data)))
        return 0
    with open(TARGET, 'w', newline='\n') as handle:
        handle.write(text)
    print('%s written (%d bytes of model)' % (os.path.relpath(TARGET, ROOT), len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
