#!/usr/bin/env python3
"""Validate GameData/datasets.json against the filesystem and the code.

Checks, in order:
  1. datasets.json parses, and every file a unit lists exists on disk.
  2. Every unit entry's type string is registered in Outpost/Data.cpp.
  3. Every dataset's unit references resolve, its .gam exists, its base
     dataset and camchange partner are declared before it, and it has at
     most LEVEL_MAXFILES slots.
  4. Duplicate (type, filename) entries within one unit are reported.

Resolution is case-insensitive, because that is what the game does on NTFS.
A name that only resolves case-insensitively is a warning; a name that does
not resolve at all is an error.  Exit code is non-zero if any error was
found.

Run from the repository root:  python tools/validate_assets.py
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = os.path.join(ROOT, 'GameData')
LEVEL_MAXFILES = 9

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
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    return set(re.findall(r'\{\s*"([A-Z_0-9]+)"', body))


DATASET_KINDS = {
    'level', 'campaign', 'camstart', 'camchange', 'expand', 'between',
    'miss_keep', 'miss_clear', 'expand_limbo', 'miss_keep_limbo', 'multi',
}


def check_units(doc, types):
    units = doc.get('units')
    if not isinstance(units, dict):
        errors.append('datasets.json: no unit table')
        return {}
    for name, entries in units.items():
        if not isinstance(entries, list):
            errors.append(f'unit {name}: not a list')
            continue
        seen = set()
        for entry in entries:
            d, t, f = entry.get('d'), entry.get('t'), entry.get('f')
            if not isinstance(t, str) or not isinstance(f, str) or not isinstance(d, str):
                errors.append(f'unit {name}: malformed entry {entry}')
                continue
            if t not in types:
                errors.append(f'unit {name}: unknown resource type {t} for "{f}"')
            key = (t, f.lower())
            if key in seen:
                warnings.append(f'unit {name}: duplicate entry {t} "{f}"')
            seen.add(key)
            ref = (d + '\\' + f) if d else f
            resolved, exact = resolve(ref)
            if resolved is None:
                if ref.lower() in KNOWN_MISSING:
                    warnings.append(f'unit {name}: known-missing (owner decision open): {ref}')
                else:
                    errors.append(f'unit {name}: referenced file not on disk: {ref}')
            elif not exact:
                warnings.append(f'unit {name}: case mismatch: {ref} -> {resolved}')
    return units


def check_datasets(doc, units):
    datasets = doc.get('datasets')
    if not isinstance(datasets, list):
        errors.append('datasets.json: no dataset list')
        return
    declared = {}
    for ds in datasets:
        name, kind = ds.get('name'), ds.get('kind')
        if not isinstance(name, str) or kind not in DATASET_KINDS:
            errors.append(f'dataset {name}: missing name or unknown kind {kind}')
            continue
        if kind == 'multi' and not isinstance(ds.get('type'), int):
            errors.append(f'dataset {name}: kind multi without a numeric type')
        if kind == 'camchange':
            if declared.get(name) != 'camstart':
                errors.append(f'dataset {name}: camchange without an earlier camstart')
        base = ds.get('base')
        if base is not None and base not in declared:
            errors.append(f'dataset {name}: base {base} not declared before it')
        slots = ds.get('slots')
        if not isinstance(slots, list) or len(slots) > LEVEL_MAXFILES:
            errors.append(f'dataset {name}: bad slot list')
            slots = []
        for slot in slots:
            if 'unit' in slot:
                if slot['unit'] not in units:
                    errors.append(f'dataset {name}: references unknown unit {slot["unit"]}')
            elif 'game' in slot:
                resolved, _exact = resolve(slot['game'])
                if resolved is None:
                    errors.append(f'dataset {name}: scenario file not on disk: {slot["game"]}')
            else:
                errors.append(f'dataset {name}: slot is neither unit nor game: {slot}')
        # first declaration wins for name lookups, as in the game
        declared.setdefault(name, kind)


def main():
    path = os.path.join(GAMEDATA, 'datasets.json')
    try:
        doc = json.load(open(path, encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as err:
        print(f'error: {path}: {err}')
        return 1
    types = registered_types()
    if types:
        units = check_units(doc, types)
        check_datasets(doc, units)
    for w in warnings:
        print(f'warning: {w}')
    for e in errors:
        print(f'error: {e}')
    print(f'{len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
