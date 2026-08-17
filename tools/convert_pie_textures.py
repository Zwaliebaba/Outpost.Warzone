#!/usr/bin/env python3
"""Rewrite the texture extension in .pie TEXTURE directives from .pcx to .dds.

A PIE 2 model names its texture page on one line:

    TEXTURE 0 page-17-droid weapons.pcx 256 256

The extension was .pcx because the art was PCX in 1998.  The palette removal
replaced every page with a DDS and deleted the PCX loader, but the models
kept saying .pcx and IMDLoad swapped the extension at parse time.  This makes
the data say what is actually on disk.

Only the extension token changes: the directive's type number, the page name
(which may contain spaces), the trailing width and height, the line ending
and every other byte of the file are left exactly as they were.

  tools/convert_pie_textures.py [--check] [GameData]

--check reports what would change and rewrites nothing; the exit code is
non-zero if any file still names a .pcx texture.
"""

import os
import re
import sys

# TEXTURE <type> <name>.<ext> <width> <height> - the name may contain spaces,
# so the extension is anchored to the two numbers that must follow it.
TEXTURE = re.compile(rb'^(TEXTURE\s+\d+\s+\S.*?)\.pcx(\s+\d+\s+\d+\s*)$',
                     re.IGNORECASE | re.MULTILINE)


def convert(root, check):
    scanned = changed = already = 0
    problems = []

    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.lower().endswith('.pie'):
                continue
            path = os.path.join(dirpath, name)
            with open(path, 'rb') as handle:
                data = handle.read()
            scanned += 1

            new, count = TEXTURE.subn(rb'\1.dds\2', data)
            if count:
                changed += 1
                # the rewrite must touch nothing but the extension
                if len(new) != len(data) - count * 4 + count * 4:
                    problems.append(f'{path}: length changed unexpectedly')
                if new.replace(b'.dds', b'.pcx', count) != data:
                    problems.append(f'{path}: rewrite altered more than the extension')
                if not check:
                    with open(path, 'wb') as handle:
                        handle.write(new)
            elif re.search(rb'^TEXTURE\b', data, re.MULTILINE | re.IGNORECASE):
                if re.search(rb'^TEXTURE\s+\d+\s+\S.*?\.dds\s+\d+\s+\d+\s*$',
                             data, re.MULTILINE | re.IGNORECASE):
                    already += 1
                else:
                    problems.append(f'{path}: TEXTURE directive matches neither .pcx nor .dds')

    verb = 'would convert' if check else 'converted'
    print(f'{scanned} .pie files scanned, {verb} {changed}, {already} already .dds')
    for p in problems:
        print(f'error: {p}')
    return 1 if problems or (check and changed) else 0


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv[1:]
    root = args[0] if args else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'GameData')
    return convert(root, check)


if __name__ == '__main__':
    sys.exit(main())
