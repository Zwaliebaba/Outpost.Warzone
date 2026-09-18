#!/usr/bin/env python3
"""Reader for the PIE 2 model format - the one the shipped .pie files use.

Two converters read these files: tools/pie_to_nmo.py (the migration, stage C of
Docs/PieToNmoMigration.md) and tools/pie_to_obj.py (a geometry export for
inspection and editing).  The parser lives here so there is one statement of
what the format is, and a quirk discovered by one converter cannot be fixed in
the other's copy.

What the format holds, and what it does not: points are integers in world
units, polygons are triangles and quads with integer texel UVs, and there are
no normals and no per-level names.  Nothing in the file says whether a
multi-level file is a stack of animation frames or a set of animated
sub-objects - that lives in the .ani data, so a reader cannot know and each
converter decides what to do about it.

All 516 shipped models are PIE 2; no PIE 3 file exists in the tree.  Version 3
changed UVs to floats and moved the texture name, so this reader would need
extending rather than tweaking if one ever arrives.
"""

import os
import re

TEXTURE_LINE = re.compile(r'^\s*TEXTURE\s+(\d+)\s+(.*?)\s+(\d+)\s+(\d+)\s*$')

PIE_TEXTURED = 0x00000200
PIE_COLOURKEYED = 0x00000800
PIE_NO_CULL = 0x00002000
PIE_TEXANIM = 0x00004000
PIE_PSXTEX = 0x00008000
PIE_BSPFRESH = 0x00010000


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


def find_pies(root):
    found = []
    for directory, _subdirs, files in os.walk(root):
        for name in files:
            if name.lower().endswith('.pie'):
                found.append(os.path.join(directory, name))
    return sorted(found)
