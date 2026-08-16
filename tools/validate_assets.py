#!/usr/bin/env python3
"""Validate the GameData asset manifests against the filesystem and the code.

Checks, in order:
  1. GameDesc.lev parses, and every .wrf/.gam it references exists.
  2. Every .wrf under GameData/WRF parses, and every file it lists exists.
  3. Every `file TYPE` string in a .wrf is a type registered in Outpost/Data.cpp.
  4. Duplicate (type, filename) entries within one .wrf are reported.

Resolution is case-insensitive, because that is what the game does: the .lev
parser lowercases every path and NTFS resolves the rest.  A name that only
resolves case-insensitively is a warning; a name that does not resolve at all
is an error.  Exit code is non-zero if any error was found.

Run from the repository root:  python tools/validate_assets.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')

errors = []
warnings = []

# Missing files the owner has not yet ruled on.  These are reported as
# warnings, not errors, so CI stays green while the decision is open.
# SUB_3_3 / SUB_3_3S: the mission's .vlo files were never in the tree, its
# brief is commented out, and no mission .slo exists - half-built content.
KNOWN_MISSING = {
    'script\\data\\cam3-3.vlo',
    'script\\data\\cam3-3s.vlo',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return text


def resolve(rel):
    """Resolve rel (with \\ or / separators) under GameData, the way the game
    would: exact first, then case-insensitively per component.
    Returns (resolved_relpath or None, case_matched_exactly)."""
    parts = [p for p in re.split(r'[\\/]', rel) if p]
    cur = GAMEDATA
    exact = True
    out = []
    for part in parts:
        cand = os.path.join(cur, part)
        if os.path.exists(cand):
            out.append(part)
            cur = cand
            continue
        try:
            entries = os.listdir(cur)
        except (FileNotFoundError, NotADirectoryError):
            return None, False
        match = next((e for e in entries if e.lower() == part.lower()), None)
        if match is None:
            return None, False
        exact = False
        out.append(match)
        cur = os.path.join(cur, match)
    return '/'.join(out), exact


def registered_types():
    """The resource types the game registers, read from Outpost/Data.cpp."""
    src = open(os.path.join(ROOT, 'Outpost', 'Data.cpp'), encoding='latin-1').read()
    m = re.search(r'ResourceTypes\[\]\s*=\s*\{(.*?)\n\};', src, flags=re.S)
    if not m:
        errors.append('Outpost/Data.cpp: could not find the ResourceTypes[] table')
        return set()
    body = strip_comments(m.group(1))
    return set(re.findall(r'\{\s*"([A-Z_0-9]+)"', body))


def check_lev():
    path = os.path.join(GAMEDATA, 'GameDesc.lev')
    text = strip_comments(open(path, encoding='latin-1').read())
    refs = re.findall(r'"([^"]+\.(?:wrf|gam))"', text, flags=re.I)
    for ref in refs:
        resolved, exact = resolve(ref)
        if resolved is None:
            errors.append(f'GameDesc.lev: referenced file not on disk: {ref}')
        elif not exact:
            warnings.append(f'GameDesc.lev: case mismatch: {ref} -> {resolved}')
    # dataset back-references must name an already-declared dataset
    names = set()
    for kind, name in re.findall(
            r'^(level|campaign|camstart|camchange|expand|between|miss_keep|'
            r'miss_clear|expand_limbo|miss_keep_limbo|dataset)\s+(\S+)',
            text, flags=re.M):
        if kind == 'dataset':
            if name not in names:
                errors.append(f'GameDesc.lev: dataset reference before declaration: {name}')
        elif kind != 'camchange':
            names.add(name)
        else:
            if name not in names:
                errors.append(f'GameDesc.lev: camchange names unknown camstart: {name}')
            names.add(name)


def check_wrfs(types):
    wrfs = []
    for dirpath, _dirnames, filenames in os.walk(os.path.join(GAMEDATA, 'WRF')):
        wrfs.extend(os.path.join(dirpath, f) for f in filenames if f.lower().endswith('.wrf'))
    statement = re.compile(r'(directory|file)\s+(?:([A-Za-z][-\w]*)\s+)?"([^"]*)"')
    for wrf in sorted(wrfs):
        rel = os.path.relpath(wrf, GAMEDATA).replace(os.sep, '/')
        text = strip_comments(open(wrf, encoding='latin-1').read())
        curdir = ''
        seen = set()
        for kind, typ, name in statement.findall(text):
            if kind == 'directory':
                curdir = name
                continue
            if typ is None or typ == '':
                errors.append(f'{rel}: file entry with no type: "{name}"')
                continue
            if typ not in types:
                errors.append(f'{rel}: unknown resource type {typ} for "{name}"')
            key = (typ, name.lower())
            if key in seen:
                warnings.append(f'{rel}: duplicate entry {typ} "{name}"')
            seen.add(key)
            ref = (curdir + '\\' + name) if curdir else name
            resolved, exact = resolve(ref)
            if resolved is None:
                if ref.lower() in KNOWN_MISSING:
                    warnings.append(f'{rel}: known-missing (owner decision open): {ref}')
                else:
                    errors.append(f'{rel}: referenced file not on disk: {ref}')
            elif not exact:
                warnings.append(f'{rel}: case mismatch: {ref} -> {resolved}')


def main():
    types = registered_types()
    if types:
        check_lev()
        check_wrfs(types)
    for w in warnings:
        print(f'warning: {w}')
    for e in errors:
        print(f'error: {e}')
    print(f'{len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
