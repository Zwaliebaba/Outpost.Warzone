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


def load_table(name):
    resolved, _exact = resolve('Stats\\' + name)
    if resolved is None:
        return None
    return json.load(open(os.path.join(GAMEDATA, resolved), encoding='utf-8'))


def check_stats(doc):
    """Cross-reference checks over the JSON stats tables: every name one
    table uses to point at another must resolve, and every model or sound a
    table names must be a unit entry the manifests actually load."""
    weapons = load_table('Weapons.json')
    if weapons is None:
        return  # tables not converted yet

    units = doc.get('units', {})
    imds = {e['f'].lower() for u in units.values() for e in u if e.get('t') == 'IMD'}
    wavs = {e['f'].lower() for u in units.values() for e in u if e.get('t') == 'WAV'}

    # Sound references resolve against the AudioID.cpp name table at load
    # time; whether the named .wav is actually loaded only matters at play
    # time, so a table hit with no loaded wav is a warning, not an error.
    audio_src = open(os.path.join(ROOT, 'Outpost', 'AudioID.cpp'), encoding='latin-1').read()
    audio_ids = {m.lower() for m in re.findall(r'"([^"]+\.wav)"', audio_src)}

    def check_sound(table, row, field, value):
        if value == '-1':
            return
        if value.lower() not in audio_ids:
            errors.append(f'Stats/{table} row {row}: {field} names a sound missing from AudioID.cpp: {value}')
        elif value.lower() not in wavs:
            warnings.append(f'Stats/{table} row {row}: {field} sound {value} is in AudioID.cpp but no unit loads that .wav - silent at runtime')

    def names(table):
        rows = load_table(table) or []
        return {r['name'] for r in rows}

    def check_ref(table, row, field, value, pool, kind, none=('0',)):
        if value in none:
            return
        if value.lower() not in pool:
            errors.append(f'Stats/{table} row {row}: {field} names a missing {kind}: {value}')

    def check_name(table, row, field, value, pool, target):
        if value == '0':
            return
        if value not in pool:
            errors.append(f'Stats/{table} row {row}: {field} names an unknown {target}: {value}')

    body_names = names('Body.json')
    prop_names = names('Propulsion.json')
    weapon_names = {r['name'] for r in weapons}
    template_names = names('Templates.json')
    structure_names = names('Structures.json')
    sensor_names = names('Sensor.json')
    ecm_names = names('ECM.json')
    brain_names = names('Brain.json')
    construct_names = names('Construction.json')
    repair_names = names('Repair.json')
    proptype_names = names('PropulsionType.json')

    model_fields = {
        'Weapons.json': ['model', 'mountModel', 'muzzleModel', 'flightModel',
                         'hitModel', 'missModel', 'waterModel', 'trailModel'],
        'Body.json': ['model', 'flameModel'],
        'Propulsion.json': ['model'],
        'Sensor.json': ['model', 'mountModel'],
        'ECM.json': ['model', 'mountModel'],
        'Repair.json': ['model', 'mountModel'],
        'Construction.json': ['model', 'mountModel'],
        'Structures.json': ['model', 'baseModel'],
        'Features.json': ['model'],
        'BodyPropulsionIMD.json': ['leftModel', 'rightModel'],
    }
    for table, fields in model_fields.items():
        for n, row in enumerate(load_table(table) or []):
            for field in fields:
                check_ref(table, n, field, row[field], imds, 'model')

    for n, row in enumerate(load_table('WeaponSounds.json') or []):
        check_name('WeaponSounds.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
        for field in ('fireSound', 'impactSound'):
            check_sound('WeaponSounds.json', n, field, row[field])
    for n, row in enumerate(load_table('PropulsionSounds.json') or []):
        check_name('PropulsionSounds.json', n, 'propulsion', row['propulsion'], proptype_names, 'propulsion type')
        for field in ('start', 'idle', 'moveOff', 'move', 'hiss', 'shutDown'):
            check_sound('PropulsionSounds.json', n, field, row[field])
    for n, row in enumerate(load_table('WeaponModifier.json') or []):
        check_name('WeaponModifier.json', n, 'propulsion', row['propulsion'], proptype_names, 'propulsion type')
    for n, row in enumerate(load_table('BodyPropulsionIMD.json') or []):
        check_name('BodyPropulsionIMD.json', n, 'body', row['body'], body_names, 'body')
        check_name('BodyPropulsionIMD.json', n, 'propulsion', row['propulsion'], prop_names, 'propulsion')
    for n, row in enumerate(load_table('Templates.json') or []):
        check_name('Templates.json', n, 'body', row['body'], body_names, 'body')
        check_name('Templates.json', n, 'brain', row['brain'], brain_names, 'brain')
        check_name('Templates.json', n, 'construct', row['construct'], construct_names, 'construct')
        check_name('Templates.json', n, 'ecm', row['ecm'], ecm_names, 'ecm')
        check_name('Templates.json', n, 'propulsion', row['propulsion'], prop_names, 'propulsion')
        check_name('Templates.json', n, 'repair', row['repair'], repair_names, 'repair')
        check_name('Templates.json', n, 'sensor', row['sensor'], sensor_names, 'sensor')
    for n, row in enumerate(load_table('AssignWeapons.json') or []):
        check_name('AssignWeapons.json', n, 'template', row['template'], template_names, 'template')
        check_name('AssignWeapons.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
    for n, row in enumerate(load_table('Structures.json') or []):
        check_name('Structures.json', n, 'ecm', row['ecm'], ecm_names, 'ecm')
        check_name('Structures.json', n, 'sensor', row['sensor'], sensor_names, 'sensor')
    for n, row in enumerate(load_table('StructureWeapons.json') or []):
        check_name('StructureWeapons.json', n, 'structure', row['structure'], structure_names, 'structure')
        check_name('StructureWeapons.json', n, 'weapon', row['weapon'], weapon_names, 'weapon')
    for n, row in enumerate(load_table('StructureFunctions.json') or []):
        check_name('StructureFunctions.json', n, 'structure', row['structure'], structure_names, 'structure')


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
        check_stats(doc)
    for w in warnings:
        print(f'warning: {w}')
    for e in errors:
        print(f'error: {e}')
    print(f'{len(errors)} error(s), {len(warnings)} warning(s)')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
